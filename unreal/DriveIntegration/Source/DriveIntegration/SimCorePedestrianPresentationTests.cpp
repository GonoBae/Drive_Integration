#include "SimCorePedestrianPresentationActor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "SimCorePresentation.h"
#include <limits>

namespace SimCorePedestrianPresentationTests
{
struct FTestWorld
{
	UWorld* World = nullptr;
	bool bOwnsWorldContext = false;
	explicit FTestWorld(EWorldType::Type Type = EWorldType::Game, bool bPhysics = false)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(bPhysics)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(bPhysics)
			.EnableTraceCollision(true).SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(Type, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCorePedestrianQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
		if (World && bPhysics && GEngine)
		{
			// UWorld::Tick consults the engine context even for an isolated test world.
			// Keep it registered for the lifetime of the manually stepped physics scene.
			World->SetShouldTick(false);
			GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
			bOwnsWorldContext = true;
		}
	}
	~FTestWorld()
	{
		if (bOwnsWorldContext && GEngine) GEngine->DestroyWorldContext(World);
		if (World) World->DestroyWorld(false);
	}
};

UBoxComponent* AddQueryOnlyGroundBox(
	UWorld* World, const FVector& Centre, const FVector& HalfExtent,
	const FRotator& Rotation = FRotator::ZeroRotator)
{
	if (!World) return nullptr;
	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor) return nullptr;
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None, RF_Transient);
	Actor->SetRootComponent(Box);
	Actor->AddInstanceComponent(Box);
	Box->InitBoxExtent(HalfExtent);
	Box->SetWorldLocationAndRotation(Centre, Rotation);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);
	Box->RegisterComponent();
	return Box;
}

SimCoreProtocol::FVehicleState Pedestrian(float Speed = 1.4f)
{
	SimCoreProtocol::FVehicleState State;
	State.EntityId = 2002;
	State.EntityKind = SimCoreProtocol::EEntityKind::Pedestrian;
	State.PositionEnu = FVector3d(10.0, 20.0, 0.9);
	State.LinearVelocityEnu = FVector3d(0.0, Speed, 0.0);
	State.CollisionHalfHeightMeters = 0.9f;
	State.CollisionRadiusMeters = 0.3f;
	return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianHumanoidGeometryTest,
	"DriveIntegration.PedestrianPresentation.HumanoidGeometryAndAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianHumanoidGeometryTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	bool Ok = true;
	FTransform Transform;
	Ok &= TestTrue(TEXT("Authored height scales uniformly and standing feet meet the capsule bottom"),
		SimCorePedestrianPresentation::BuildModelTransform(
			FBox(FVector(-40, -20, -5), FVector(40, 20, 195)), 0.9f, Transform)
		&& Transform.GetScale3D().Equals(FVector(0.9), 0.0001)
		&& FMath::IsNearlyEqual(Transform.TransformPosition(FVector(0, 0, -5)).Z, -90.0, 0.001)
		&& Transform.TransformVectorNoScale(FVector::RightVector).Equals(FVector::ForwardVector, 0.001));
	Ok &= TestFalse(TEXT("Invalid authored bounds are not displayed"),
		SimCorePedestrianPresentation::BuildModelTransform(FBox(ForceInit), 0.9f, Transform));
	FTestWorld Scene;
	if (!TestNotNull(TEXT("Isolated game world"), Scene.World)) return false;
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	auto* Actor = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>(
		ASimCorePedestrianPresentationActor::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Actual humanoid presentation actor"), Actor)) return false;
	Ok &= TestTrue(TEXT("Bundled human mesh and locomotion assets are present"), Actor->HasHumanoidAssets());
	Ok &= TestTrue(TEXT("Bundled physics asset contains articulated bodies and joints"), Actor->HasRagdollPhysicsAsset());
	if (!Actor->HasHumanoidAssets()) return false;
	auto* Mesh = Actor->GetCharacterMesh();
	Ok &= TestTrue(TEXT("The humanoid has actual head, hands and feet bones, not a capsule mesh"),
		Mesh->GetBoneIndex(TEXT("head")) != INDEX_NONE
		&& Mesh->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE
		&& Mesh->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE
		&& Mesh->GetBoneIndex(TEXT("foot_l")) != INDEX_NONE
		&& Mesh->GetBoneIndex(TEXT("foot_r")) != INDEX_NONE
		&& Mesh->GetNumMaterials() >= 2);
	Ok &= TestTrue(TEXT("Humanoid is visual only; the server retains all movement and collision authority"),
		Actor->HasAnyFlags(RF_Transient) && !Actor->GetIsReplicated() && !Actor->GetActorEnableCollision()
		&& Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
		&& !Mesh->IsSimulatingPhysics() && !Mesh->GetGenerateOverlapEvents()
		&& !Mesh->CanEverAffectNavigation() && Mesh->Mobility == EComponentMobility::Movable);
	auto State = Pedestrian();
	const FVector Offset(17, -23, 41);
	Ok &= TestTrue(TEXT("Accepted server snapshot positions the character"),
		Actor->ApplySnapshot(State, 0.025f, 0.02f, true, 0.05f, Offset));
	Ok &= TestTrue(TEXT("Prediction and map origin preserve the authoritative capsule centre"),
		Actor->GetActorLocation().Equals(FVector(2003.5, 1000, 90) + Offset, 0.01)
		&& Actor->GetActorScale3D().Equals(FVector::OneVector));
	const FBox Bounds = Mesh->GetSkeletalMeshAsset()->GetImportedBounds().GetBox();
	const double FeetZ = Mesh->GetComponentTransform().TransformPosition(
		FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z)).Z;
	Ok &= TestTrue(TEXT("Humanoid feet align with the server ground without a local trace"),
		FMath::IsNearlyEqual(FeetZ, Offset.Z, 0.01));
	Ok &= TestTrue(TEXT("Even entity uses Manny"),
		Mesh->GetSkeletalMeshAsset()->GetName() == TEXT("SKM_Manny_Simple"));
	State.EntityId = 2003;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Odd entity uses compatible Quinn for visible variation"),
		Mesh->GetSkeletalMeshAsset()->GetName() == TEXT("SKM_Quinn_Simple"));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianForwardAxisTest,
	"DriveIntegration.PedestrianPresentation.ServerHeadingMakesActorXForward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianForwardAxisTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	FTestWorld Scene;
	if (!Scene.World) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	if (!Actor || !Actor->HasHumanoidAssets()) return false;
	bool Ok = true;
	for (float Heading : {0.0f, 90.0f, 180.0f, 270.0f})
	{
		auto State = Pedestrian();
		State.HeadingDegrees = Heading;
		const double Radians = FMath::DegreesToRadians(Heading);
		State.LinearVelocityEnu = FVector3d(FMath::Sin(Radians), FMath::Cos(Radians), 0.0) * 1.4;
		Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
		const FVector Travel(State.LinearVelocityEnu.Y, State.LinearVelocityEnu.X, 0.0);
		Ok &= TestTrue(TEXT("North/east/south/west travel follows actor +X, never local +Y"),
			FVector::DotProduct(Actor->GetActorForwardVector(), Travel.GetSafeNormal()) > 0.999);
		Ok &= TestTrue(TEXT("Manny authored +Y front is corrected to the actor +X front"),
			Actor->GetCharacterMesh()->GetComponentTransform().TransformVectorNoScale(FVector::RightVector)
				.GetSafeNormal().Equals(Actor->GetActorForwardVector(), 0.001));
		State.LinearVelocityEnu = FVector3d(-1.0, 0.0, 0.0); // sideways impact does not rotate the capsule heading
		Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
		Ok &= TestTrue(TEXT("A reaction velocity never overwrites authoritative facing"),
			FVector::DotProduct(Actor->GetActorForwardVector(), Travel.GetSafeNormal()) > 0.999);
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianRagdollTest,
	"DriveIntegration.PedestrianPresentation.ArticulatedImpactAuthorityAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianRagdollTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	FTestWorld Scene(EWorldType::Game, true);
	if (!TestNotNull(TEXT("Isolated physics scene"), Scene.World)) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	if (!Actor || !Actor->HasHumanoidAssets() || !Actor->HasRagdollPhysicsAsset()) return false;
	bool Ok = true;
	auto State = Pedestrian(0.0f);
	State.PlaySessionId = TEXT("ragdoll-play-a");
	State.MapPackageChecksum = TEXT("map-a");
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	State.LastImpactImpulseNs = 240.0f;
	State.DamagePercent = 50.0f;
	State.CollisionEventSequence = 1;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Settling;
	State.bPedestrianDowned = true;
	State.bPedestrianAirborne = true;
	State.CollisionRadiusMeters = 0.0f;
	State.CollisionHalfLengthMeters = 0.18f;
	State.CollisionHalfWidthMeters = 0.35f;
	State.CollisionHalfHeightMeters = 0.90f;
	State.ImpactDirectionEnu = FVector3d(1.0, 0.0, 0.0);
	State.LinearVelocityEnu = FVector3d(3.0, 0.0, 0.0);
	State.AngularVelocityBody = FVector3d(0.0, -4.0, 0.0);
	const FVector Launch = SimCorePedestrianPresentation::BuildImpactLaunchVelocity(State, FVector::ForwardVector);
	Ok &= TestTrue(TEXT("East server momentum maps to Unreal +Y without an invented upward or forward kick"),
		Launch.Equals(FVector(0, 300, 0), 0.001));
	const FVector LowStrikeOmega = SimCorePedestrianPresentation::BuildImpactAngularVelocity(State);
	Ok &= TestTrue(TEXT("Low-body impact retains the signed nose-up rotation toward the hood"),
		LowStrikeOmega.Equals(FVector(0,-4,0), 0.001));
	auto HighStrike = State;
	HighStrike.AngularVelocityBody.Y = 4.0;
	Ok &= TestTrue(TEXT("Opposite contact torque reverses the visual angular velocity instead of always ejecting forward"),
		SimCorePedestrianPresentation::BuildImpactAngularVelocity(HighStrike).Equals(-LowStrikeOmega, 0.001));
	HighStrike.AngularVelocityBody.Y = 40.0;
	Ok &= TestTrue(TEXT("An extreme protocol-valid body rate is bounded before it can make limbs look weightless"),
		FMath::IsNearlyEqual(SimCorePedestrianPresentation::BuildImpactAngularVelocity(HighStrike).Size(), 7.0, 0.001));
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	auto* Mesh = Actor->GetCharacterMesh();
	Ok &= TestTrue(TEXT("Collision releases spine, arms and legs while the authoritative pelvis remains kinematic"),
		Actor->IsRagdollActive() && Actor->GetRagdollLaunchCount() == 1
		&& !Mesh->IsSimulatingPhysics(TEXT("pelvis")) && Mesh->IsSimulatingPhysics(TEXT("head"))
		&& Mesh->Bodies.Num() >= 10 && Mesh->Constraints.Num() >= 8);
	Ok &= TestTrue(TEXT("The actual physics asset has a simulated head in the launched upper-body chain"),
		Mesh->GetBodyInstance(TEXT("head")) != nullptr && Mesh->IsSimulatingPhysics(TEXT("head"))
		&& Mesh->BoneIsChildOf(TEXT("head"), TEXT("spine_03")));
	Ok &= TestTrue(TEXT("Upper body receives a different launch from the pelvis, rather than rigid capsule motion"),
		!Mesh->GetPhysicsLinearVelocity(TEXT("head")).Equals(Mesh->GetPhysicsLinearVelocity(TEXT("pelvis")), 1.0));
	Ok &= TestTrue(TEXT("The articulated visual only collides with static support, not vehicles or query sensors"),
		Mesh->GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly
		&& Mesh->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block
		&& Mesh->GetCollisionResponseToChannel(ECC_Vehicle) == ECR_Ignore
		&& Mesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
		&& Mesh->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore
		&& Mesh->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Ignore
		&& !Mesh->GetGenerateOverlapEvents());
	for (int32 Index = 0; Index < 20; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Repeated accepted snapshots never replay the same impact launch"), Actor->GetRagdollLaunchCount() == 1);
	State.PositionEnu.X += 1.0;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Server +X-forward root is authoritative even while its skeletal mesh tumbles"),
		Actor->GetActorLocation().Equals(FVector(2000, 1100, 90), 0.01)
		&& Actor->GetActorForwardVector().Equals(FVector::ForwardVector, 0.001));
	Actor->ApplySnapshot(State, 0.0f, 0.02f, false, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Safe stop freezes ragdoll without restoring walking"),
		Actor->IsRagdollActive() && Mesh->bPauseAnims && !Mesh->IsGravityEnabled());
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Disabled;
	State.bPedestrianAirborne = false;
	for (int32 Index = 0; Index < 180; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.05f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("A severe server-disabled pedestrian stays down past the recovery timeout"), Actor->IsRagdollActive());
	State.CollisionEventSequence = 2;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("A separately numbered second collision can move an already downed body"), Actor->GetRagdollLaunchCount() == 2);
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Recovering;
	State.bPedestrianDowned = false;
	State.CollisionRadiusMeters = 0.3f;
	State.CollisionHalfHeightMeters = 0.9f;
	// A fresh second impact resets the minimum downed dwell. Let that two-second
	// authority fence elapse, then sample the early eased stand-up transition.
	for (int32 Index = 0; Index < 46; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.05f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Recovery first settles, then eases from ragdoll toward standing without a one-frame snap"),
		Actor->IsRagdollActive() && Actor->IsRecoveringRagdoll()
		&& Actor->GetRecoveryPoseAlpha() > 0.0f
		&& Actor->GetRecoveryPoseAlpha() < 1.0f);
	for (int32 Index = 0; Index < 27; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.05f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Only server-authorized recovery blends back and remains recovered without event replay"),
		!Actor->IsRagdollActive() && !Mesh->IsSimulatingPhysics()
		&& Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
		&& Mesh->GetAttachParent() == Actor->GetRootComponent() && Actor->GetRagdollLaunchCount() == 2);
	Ok &= TestTrue(TEXT("A recovered person pauses upright before resuming a received walking velocity"),
		!Actor->IsWalking() && Actor->GetRecoveryWalkDelaySeconds() > 0.0f);
	for (int32 Index = 0; Index < 12; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.05f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Walking resumes only after the post-recovery balance pause"),
		Actor->IsWalking() && Actor->GetRecoveryWalkDelaySeconds() == 0.0f);
	State.CollisionEventSequence = 3;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Disabled;
	State.bPedestrianDowned = true;
	State.CollisionRadiusMeters = 0.0f;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Stale state cleans up local physics and hides the character"),
		!Actor->ApplySnapshot(State, 0.101f, 0.02f, true, 0.05f, FVector::ZeroVector)
		&& !Actor->IsRagdollActive() && !Mesh->IsSimulatingPhysics() && Actor->IsHidden());
	const uint32 LaunchesBeforeFreshState = Actor->GetRagdollLaunchCount();
	for (auto Phase : {SimCoreProtocol::ERuntimeRecoveryPhase::Disabled,
		SimCoreProtocol::ERuntimeRecoveryPhase::Settling})
	{
		State.RuntimeRecoveryPhase = Phase;
		Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
		Ok &= TestTrue(TEXT("Fresh same-event disabled or settling state restores downed physics without relaunch"),
			Actor->IsRagdollActive() && !Mesh->IsSimulatingPhysics(TEXT("pelvis"))
			&& Mesh->IsSimulatingPhysics(TEXT("head"))
			&& !Actor->IsHidden() && Actor->GetRagdollLaunchCount() == LaunchesBeforeFreshState);
		Actor->ApplySnapshot(State, 0.101f, 0.02f, true, 0.05f, FVector::ZeroVector);
	}
	State.PlaySessionId = TEXT("ragdoll-play-b");
	State.CollisionEventSequence = 0;
	State.LastImpactImpulseNs = 0.0f;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Driving;
	State.bPedestrianDowned = false;
	State.CollisionRadiusMeters = 0.3f;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("A new Play starts from an intact standing actor and empty event history"),
		!Actor->IsRagdollActive() && Actor->GetRagdollLaunchCount() == 0 && !Actor->IsHidden());
	auto* Restored = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	State.CollisionEventSequence = 7;
	State.LastImpactImpulseNs = 600.0f;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Disabled;
	State.bPedestrianDowned = true;
	State.CollisionRadiusMeters = 0.0f;
	Restored->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Reconnect into old damage reconstructs downed physics without re-firing its launch"),
		Restored->IsRagdollActive() && Restored->GetRagdollLaunchCount() == 0);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianGroundAnchorMathTest,
	"DriveIntegration.PedestrianPresentation.GravityLimitedGroundAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianGroundAnchorMathTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	float DownwardSpeed = 0.0f;
	float Height = 30.0f;
	float PreviousStep = 0.0f;
	for (int32 Frame = 0; Frame < 60 && Height > 0.0f; ++Frame)
	{
		const float Step = SimCorePedestrianPresentation::BuildSettledGroundAnchorStep(
			0.0f, Height, 1.0f / 60.0f, DownwardSpeed);
		if (Step == 0.0f) break;
		Ok &= TestTrue(TEXT("A floating settled body can move only downward"), Step <= 0.0f);
		Ok &= TestTrue(TEXT("Ground correction accelerates instead of teleporting"),
			Frame == 0 || FMath::Abs(Step) + 0.001f >= FMath::Abs(PreviousStep)
				|| Height + Step <= SimCorePedestrianPresentation::GroundContactToleranceCm);
		Height += Step;
		PreviousStep = Step;
	}
	Ok &= TestTrue(TEXT("Gravity-limited correction reaches the authoritative floor tolerance"),
		Height <= SimCorePedestrianPresentation::GroundContactToleranceCm + 0.001f);
	const float AtGround = SimCorePedestrianPresentation::BuildSettledGroundAnchorStep(
		0.0f, 0.5f, 1.0f / 60.0f, DownwardSpeed);
	Ok &= TestTrue(TEXT("A grounded body remains still without accumulating hidden velocity"),
		AtGround == 0.0f && DownwardSpeed == 0.0f);
	DownwardSpeed = 10.0f;
	Ok &= TestTrue(TEXT("Invalid timing fails closed"),
		SimCorePedestrianPresentation::BuildSettledGroundAnchorStep(
			0.0f, 30.0f, -1.0f, DownwardSpeed) == 0.0f && DownwardSpeed == 0.0f);
	const FVector SlopeNormal = FVector(0.0, -0.5, 0.8660254).GetSafeNormal();
	const FVector Depenetration = SimCorePedestrianPresentation::BuildLocalGroundDepenetrationStep(
		10.0f, 0.0f, SlopeNormal, 1.0f / 60.0f);
	Ok &= TestTrue(TEXT("Local penetration resolves along the measured slope normal"),
		Depenetration.GetSafeNormal().Equals(SlopeNormal, 0.001));
	Ok &= TestTrue(TEXT("Deep local penetration is resolved over time instead of snapping"),
		Depenetration.Size() <= 350.0f / 60.0f + 0.001f);
	const FVector SlidingVelocity(120.0, 50.0, -300.0);
	const FVector ContactVelocity = SimCorePedestrianPresentation::RemoveVelocityIntoGround(
		SlidingVelocity, SlopeNormal);
	Ok &= TestTrue(TEXT("Slope contact removes only inward normal speed and preserves tangential motion"),
		FMath::IsNearlyZero(FVector::DotProduct(ContactVelocity, SlopeNormal), 0.001)
		&& FVector::CrossProduct(ContactVelocity, SlopeNormal).Size() > 1.0f);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianLocalGroundQueryTest,
	"DriveIntegration.PedestrianPresentation.LocalSlopeCurbAndUnevenGroundQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianLocalGroundQueryTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	FTestWorld Scene(EWorldType::Game, true);
	if (!TestNotNull(TEXT("Isolated collision-query world"), Scene.World)) return false;
	bool Ok = true;
	Ok &= TestNotNull(TEXT("Flat road fixture"), AddQueryOnlyGroundBox(
		Scene.World, FVector(300.0, 0.0, -10.0), FVector(120.0, 120.0, 10.0)));
	Ok &= TestNotNull(TEXT("Raised curb fixture"), AddQueryOnlyGroundBox(
		Scene.World, FVector(300.0, 0.0, 10.0), FVector(35.0, 45.0, 10.0)));
	Ok &= TestNotNull(TEXT("Sloped query-only fixture"), AddQueryOnlyGroundBox(
		Scene.World, FVector(-300.0, 0.0, -10.0), FVector(100.0, 100.0, 10.0),
		FRotator(0.0, 0.0, 15.0)));
	auto QueryAt = [&](double X, double Y, FVector& OutPoint, FVector& OutNormal)
	{
		const FVector Centre(X, Y, 80.0);
		return SimCorePedestrianPresentation::QueryLocalGroundSupport(
			Scene.World, nullptr, FBox(Centre - FVector(5.0, 5.0, 10.0),
				Centre + FVector(5.0, 5.0, 10.0)), 0.0f, OutPoint, OutNormal);
	};
	FVector CurbPoint;
	FVector CurbNormal;
	FVector RoadPoint;
	FVector RoadNormal;
	Ok &= TestTrue(TEXT("A body over the curb measures the curb top"),
		QueryAt(300.0, 0.0, CurbPoint, CurbNormal)
		&& FMath::IsNearlyEqual(CurbPoint.Z, 20.0, 0.1));
	Ok &= TestTrue(TEXT("A neighbouring body measures the lower road independently"),
		QueryAt(380.0, 0.0, RoadPoint, RoadNormal)
		&& FMath::IsNearlyEqual(RoadPoint.Z, 0.0, 0.1)
		&& CurbPoint.Z - RoadPoint.Z > 19.0);
	FVector SlopeLowPoint;
	FVector SlopeLowNormal;
	FVector SlopeHighPoint;
	FVector SlopeHighNormal;
	Ok &= TestTrue(TEXT("Separate limb probes follow different heights on one slope"),
		QueryAt(-300.0, -45.0, SlopeLowPoint, SlopeLowNormal)
		&& QueryAt(-300.0, 45.0, SlopeHighPoint, SlopeHighNormal)
		&& FMath::Abs(SlopeHighPoint.Z - SlopeLowPoint.Z) > 15.0);
	Ok &= TestTrue(TEXT("Slope probes retain the collision normal instead of fabricating a flat plane"),
		SlopeLowNormal.Z < 0.99 && SlopeLowNormal.Z > 0.9
		&& SlopeLowNormal.Equals(SlopeHighNormal, 0.001));
	FVector MissingPoint;
	FVector MissingNormal;
	Ok &= TestFalse(TEXT("No collision hit means no invented actor-Z support plane"),
		QueryAt(0.0, 0.0, MissingPoint, MissingNormal));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianGentleContactTest,
	"DriveIntegration.PedestrianPresentation.GentleShoveAndSameEventKnockdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianGentleContactTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	FTestWorld Scene(EWorldType::Game, true);
	if (!Scene.World) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	if (!Actor || !Actor->HasHumanoidAssets() || !Actor->HasRagdollPhysicsAsset()) return false;
	bool Ok = true;
	auto State = Pedestrian(0.0f);
	State.PlaySessionId = TEXT("gentle-contact-play");
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	auto* Mesh = Actor->GetCharacterMesh();
	State.CollisionEventSequence = 1;
	State.LastImpactImpulseNs = 60.0f;
	State.ImpactDirectionEnu = FVector3d(1.0, 0.0, 0.0);
	State.LinearVelocityEnu = FVector3d(0.5, 0.0, 0.0);
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Settling;
	State.PositionEnu.X += 0.3;
	for (int32 Index = 0; Index < 30; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("A repeated mild collision moves the standing person without releasing the skeleton"),
		Actor->GetActorLocation().Equals(FVector(2000, 1030, 90), 0.01)
		&& !Actor->IsRagdollActive() && Actor->GetRagdollLaunchCount() == 0
		&& !Mesh->IsSimulatingPhysics(TEXT("head"))
		&& Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Actor->IsWalking());
	Ok &= TestTrue(TEXT("Standing collision metadata cannot independently invent a visual launch"),
		SimCorePedestrianPresentation::BuildImpactLaunchVelocity(State, FVector::ForwardVector).IsZero());
	State.DamagePercent = 100.0f;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Disabled;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Damage or route-disable metadata never substitutes for the server downed decision"),
		!Actor->IsRagdollActive() && Actor->GetRagdollLaunchCount() == 0);
	Actor->ApplySnapshot(State, 0.101f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	auto* Restored = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	if (!Restored) return false;
	Restored->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Fresh or reconnected mild-contact state stays standing without an old-event launch"),
		!Actor->IsHidden() && !Actor->IsRagdollActive() && !Restored->IsRagdollActive()
		&& Actor->GetRagdollLaunchCount() == 0 && Restored->GetRagdollLaunchCount() == 0);
	State.LastImpactImpulseNs = 160.0f;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Settling;
	State.bPedestrianDowned = true;
	State.CollisionRadiusMeters = 0.0f;
	State.CollisionHalfLengthMeters = 0.4f;
	State.CollisionHalfWidthMeters = 0.35f;
	State.CollisionHalfHeightMeters = 0.98f;
	State.PitchDegrees = -15.0f;
	Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("The first server downed transition releases joints immediately even with the same event number"),
		Actor->IsRagdollActive() && Actor->GetRagdollLaunchCount() == 1
		&& Mesh->IsSimulatingPhysics(TEXT("head")) && !Mesh->IsSimulatingPhysics(TEXT("pelvis")));
	State.LastImpactImpulseNs = 640.0f;
	State.bPedestrianAirborne = true;
	State.LinearVelocityEnu.Z = 2.8;
	for (int32 Index = 0; Index < 10; ++Index)
		Actor->ApplySnapshot(State, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("A stronger peak of an already downed event never re-fires the visual impulse"),
		Actor->GetRagdollLaunchCount() == 1);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianHumanoidAnimationTest,
	"DriveIntegration.PedestrianPresentation.AcceptedVelocityDrivesLocomotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianHumanoidAnimationTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	FTestWorld Scene;
	if (!Scene.World) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	if (!Actor || !Actor->HasHumanoidAssets()) return false;
	bool Ok = true;
	auto* Mesh = Actor->GetCharacterMesh();
	Actor->ApplySnapshot(Pedestrian(), 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	auto* Animation = Mesh->GetSingleNodeInstance();
	if (!TestNotNull(TEXT("Skeleton has a real animation instance"), Animation)) return false;
	Ok &= TestTrue(TEXT("Normal crossing velocity plays the bundled forward-walk clip"), Actor->IsWalking()
		&& FMath::IsNearlyEqual(Actor->GetAnimationPlayRate(), 1.0f)
		&& Animation->GetAnimationAsset()->GetName() == TEXT("MF_Unarmed_Walk_Fwd"));
	Actor->ApplySnapshot(Pedestrian(0.7f), 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Half walking speed halves cadence without moving the actor locally"),
		FMath::IsNearlyEqual(Actor->GetAnimationPlayRate(), 0.5f)
		&& Actor->GetActorLocation().Equals(FVector(2000, 1000, 90), 0.01));
	Actor->ApplySnapshot(Pedestrian(0.0f), 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Signal wait or settled collision displays idle, never walking in place"),
		!Actor->IsWalking() && Mesh->GetSingleNodeInstance()->GetAnimationAsset()->GetName() == TEXT("MM_Idle"));
	Actor->ApplySnapshot(Pedestrian(), 0.04f, 0.02f, false, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Safe stop or inactive lease pauses animation and extrapolation"),
		!Actor->IsWalking() && Mesh->bPauseAnims && Actor->GetAnimationPlayRate() == 0.0f
		&& Actor->GetActorLocation().Equals(FVector(2000, 1000, 90), 0.01));
	for (float Age : {-0.001f, 0.101f, std::numeric_limits<float>::quiet_NaN()})
	{
		Ok &= TestFalse(TEXT("Invalid or stale snapshot cannot keep a humanoid visible"),
			Actor->ApplySnapshot(Pedestrian(), Age, 0.02f, true, 0.05f, FVector::ZeroVector));
		Ok &= TestTrue(TEXT("Rejected character is hidden and animation paused"), Actor->IsHidden() && Mesh->bPauseAnims);
	}
	FTestWorld EditorScene(EWorldType::Editor);
	if (!EditorScene.World) return false;
	auto* EditorActor = EditorScene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	Ok &= TestTrue(TEXT("Runtime humanoids never animate in the editor world"), EditorActor
		&& !EditorActor->ApplySnapshot(Pedestrian(), 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector)
		&& EditorActor->IsHidden());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePedestrianMovingImpactTest,
	"DriveIntegration.PedestrianPresentation.ImmediateMovingImpactAndAuthoritativeDownedBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePedestrianMovingImpactTest::RunTest(const FString& Parameters)
{
	using namespace SimCorePedestrianPresentationTests;
	FTestWorld Scene(EWorldType::Game, true);
	if (!Scene.World) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCorePedestrianPresentationActor>();
	if (!Actor || !Actor->HasRagdollPhysicsAsset()) return false;
	auto State = Pedestrian(0.0f);
	Actor->ApplySnapshot(State, 0.0f, 1.0f / 60.0f, true, 0.05f, FVector::ZeroVector);
	auto* Mesh = Actor->GetCharacterMesh();
	const FVector StandingScale = Mesh->GetComponentScale();
	State.CollisionEventSequence = 1;
	State.LastImpactImpulseNs = 480.0f;
	State.ImpactDirectionEnu = FVector3d(1.0, 0.0, 0.0);
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Settling;
	State.bPedestrianDowned = true;
	State.bPedestrianAirborne = true;
	State.HeadingDegrees = 90.0f;
	State.PitchDegrees = -20.0f;
	State.CollisionRadiusMeters = 0.0f;
	State.CollisionHalfLengthMeters = 0.6f;
	State.CollisionHalfWidthMeters = 0.35f;
	State.CollisionHalfHeightMeters = 0.95f;
	State.LinearVelocityEnu = FVector3d(6.0, 0.0, 3.0);
	State.AngularVelocityBody = FVector3d(0.0, 4.0, 0.0);
	State.PositionEnu.Z = 1.0;
	bool Ok = TestTrue(TEXT("The very first moving impact snapshot activates ragdoll, without waiting for the car to stop"),
		Actor->ApplySnapshot(State, 0.0f, 1.0f / 60.0f, true, 0.05f, FVector::ZeroVector)
		&& Actor->IsRagdollActive() && Actor->GetRagdollLaunchCount() == 1);
	FBodyInstance* Head = Mesh->GetBodyInstance(TEXT("head"));
	FBodyInstance* Pelvis = Mesh->GetBodyInstance(TEXT("pelvis"));
	if (!TestNotNull(TEXT("Head physics body"), Head) || !TestNotNull(TEXT("Pelvis authority body"), Pelvis)) return false;
	const FVector HeadArm = Head->GetUnrealWorldTransform().GetLocation() - Mesh->GetSocketLocation(TEXT("pelvis"));
	const FVector ExpectedHeadVelocity = (SimCorePedestrianPresentation::BuildImpactLaunchVelocity(State, Actor->GetActorForwardVector())
		+ FVector::CrossProduct(SimCorePedestrianPresentation::BuildImpactAngularVelocity(State), HeadArm)).GetClampedToMaxSize(7000.0);
	Ok &= TestTrue(TEXT("Initial head velocity is the received centre velocity plus signed omega cross lever, without a second forward/up kick"),
		Head->GetUnrealWorldVelocity().Equals(ExpectedHeadVelocity, 0.1));
	Ok &= TestTrue(TEXT("The visible pelvis and authoritative body centre coincide at impact"),
		Mesh->GetSocketLocation(TEXT("pelvis")).Equals(Actor->GetActorLocation(), 0.1));
	const FVector HeadBeforeMovingSnapshot = Head->GetUnrealWorldTransform().GetLocation();
	State.PositionEnu.X += 0.20;
	State.PositionEnu.Z += 0.10;
	Actor->ApplySnapshot(State, 0.0f, 1.0f / 60.0f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("A moving server pelvis does not teleport the simulated head/body after it"),
		Head->GetUnrealWorldTransform().GetLocation().Equals(HeadBeforeMovingSnapshot, 0.1));
	Ok &= TestTrue(TEXT("Pelvis follows the body centre, without a displaced upright ghost anchor"),
		Mesh->GetSocketLocation(TEXT("pelvis")).Equals(Actor->GetActorLocation(), 0.1)
		&& !Pelvis->IsInstanceSimulatingPhysics());
	for (int32 Frame = 1; Frame <= 24; ++Frame)
	{
		const double Time = Frame / 60.0;
		State.PositionEnu = FVector3d(10.2 + 6.0 * Time, 20.0, 1.1 + 3.0 * Time - 4.905 * Time * Time);
		State.LinearVelocityEnu = FVector3d(6.0, 0.0, 3.0 - 9.81 * Time);
		State.PitchDegrees = -FMath::Min(90.0, 20.0 + Time * 150.0);
		Actor->ApplySnapshot(State, 0.0f, 1.0f / 60.0f, true, 0.05f, FVector::ZeroVector);
		Scene.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	}
	Ok &= TestTrue(TEXT("Physics frames actually move the free limb bodies while impact launch stays single-shot"),
		!Head->GetUnrealWorldTransform().GetLocation().Equals(HeadBeforeMovingSnapshot, 1.0)
		&& Actor->GetRagdollLaunchCount() == 1 && Actor->IsRagdollActive());
	Ok &= TestTrue(TEXT("After real physics frames the kinematic pelvis body still coincides with the server collider centre"),
		Pelvis->GetUnrealWorldTransform().GetLocation().Equals(Actor->GetActorLocation(), 1.0));
	Ok &= TestTrue(TEXT("Airborne skeleton keeps adult scale instead of shrinking to the collision height"),
		Mesh->GetComponentScale().Equals(StandingScale, 0.001));
	State.bPedestrianAirborne = false;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Disabled;
	State.PitchDegrees = -90.0f;
	State.PositionEnu.Z = 0.18;
	State.LinearVelocityEnu = FVector3d::ZeroVector;
	State.CollisionHalfLengthMeters = 0.90f;
	State.CollisionHalfHeightMeters = 0.18f;
	Actor->ApplySnapshot(State, 0.0f, 1.0f / 60.0f, true, 0.05f, FVector::ZeroVector);
	SimCorePresentation::FRuntimeEntityPresentationSample DownedSample;
	Ok &= TestTrue(TEXT("Downed collider is a grounded 180x70x36 cm box with zero standing capsule radius"),
		SimCorePresentation::BuildRuntimeEntitySample(State, 0.0f, 0.05f, FVector::ZeroVector, DownedSample)
		&& DownedSample.ActorScale.Equals(FVector(1.8, 0.7, 0.36), 0.001)
		&& Mesh->GetComponentScale().Equals(StandingScale, 0.001));
	Ok &= TestTrue(TEXT("A ninety-degree fall places the body/head axis along the impact direction, not upright"),
		FVector::DotProduct(Actor->GetActorUpVector(), FVector::RightVector) > 0.999
		&& Mesh->GetSocketLocation(TEXT("pelvis")).Equals(Actor->GetActorLocation(), 0.1));
	for (float Heading : {0.0f, 90.0f, 180.0f, 270.0f})
	{
		State.HeadingDegrees = Heading;
		SimCorePresentation::BuildRuntimeEntitySample(State, 0.0f, 0.05f, FVector::ZeroVector, DownedSample);
		const FVector Impact = FRotator(0.0, Heading, 0.0).Vector();
		Ok &= TestTrue(TEXT("Downed pose and yaw-only projected collision length agree in all cardinal headings"),
			FVector::DotProduct(DownedSample.ActorRotation.Quaternion().GetUpVector(), Impact) > 0.999);
	}
	return Ok;
}

#endif
