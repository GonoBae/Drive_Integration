#include "SimCoreDriverPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PoseableMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "ExternalVehiclePawn.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCoreSedanVisualContract.h"
#include <limits>

namespace
{
struct FDriverTestWorld
{
	UWorld* World = nullptr;
	FDriverTestWorld(const bool bCreatePhysicsScene = false)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(bCreatePhysicsScene)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
			.SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
				TEXT("DriverPresentationQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	}
	~FDriverTestWorld() { if (World) World->DestroyWorld(false); }
};

SimCoreProtocol::FVehicleState NpcState()
{
	SimCoreProtocol::FVehicleState State;
	State.EntityId = 71;
	State.EntityKind = SimCoreProtocol::EEntityKind::NpcVehicle;
	State.PlaySessionId = TEXT("driver-test");
	State.MapPackageChecksum = TEXT("fnv1a64:driver-test");
	State.PositionEnu = FVector3d(10.0, 20.0, 0.85);
	State.CollisionHalfLengthMeters = 2.2f;
	State.CollisionHalfWidthMeters = 1.0f;
	State.CollisionHalfHeightMeters = 0.75f;
	return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriverTargetTest,
	"DriveIntegration.Presentation.Driver.AuthoritativeInjuryTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriverTargetTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	FVehicleState State;
	auto Target = SimCoreDriverPresentation::BuildTarget(State);
	bool Ok = TestEqual(TEXT("undamaged reset is upright"), Target.InjuryAlpha, 0.0f);

	State.CollisionEventSequence = 3;
	State.LastImpactImpulseNs = 10000.0f;
	State.DamagePercent = 90.0f;
	State.DamageZone = EVehicleDamageZone::Left;
	Target = SimCoreDriverPresentation::BuildTarget(State);
	Ok &= TestTrue(TEXT("severe authoritative collision produces a strong slump"),
		Target.InjuryAlpha > 0.9f);
	Ok &= TestTrue(TEXT("left impact adds a bounded lateral lean"),
		Target.LateralLeanDegrees < 0.0f
		&& FMath::Abs(Target.LateralLeanDegrees) <= 10.0f);

	State = {};
	State.RuntimeRecoveryPhase = ERuntimeRecoveryPhase::Disabled;
	State.DamagePercent = 80.0f;
	Target = SimCoreDriverPresentation::BuildTarget(State);
	Ok &= TestTrue(TEXT("severely damaged disabled vehicle retains an injured driver"),
		Target.InjuryAlpha >= 0.75f);
	State = {};
	State.EntityKind = EEntityKind::NpcVehicle;
	State.CollisionEventSequence = 1;
	State.LastImpactImpulseNs = 3000.0f;
	State.DamagePercent = 10.0f;
	State.RuntimeRecoveryPhase = ERuntimeRecoveryPhase::Holding;
	Target = SimCoreDriverPresentation::BuildTarget(State);
	Ok &= TestTrue(TEXT("moderate settled NPC crash chooses protest instead of collapse"),
		Target.InjuryAlpha == 0.0f && Target.ProtestAlpha == 1.0f);
	State.DamagePercent = std::numeric_limits<float>::quiet_NaN();
	Ok &= TestEqual(TEXT("invalid authority fails safe to upright"),
		SimCoreDriverPresentation::BuildTarget(State).InjuryAlpha, 0.0f);

	const float OneStep = SimCoreDriverPresentation::AdvanceTowards(0.0f, 1.0f, 0.1f, 5.0f);
	Ok &= TestTrue(TEXT("pose transition is smooth and bounded"), OneStep > 0.0f && OneStep < 1.0f);
	const SimCoreDriverPresentation::FExitSequencePose ReachingForHandle =
		SimCoreDriverPresentation::EvaluateExitSequence(0.04f);
	Ok &= TestTrue(TEXT("driver reaches the handle before the door moves"),
		ReachingForHandle.DoorOpenAlpha == 0.0f
		&& ReachingForHandle.DriverExitAlpha == 0.0f
		&& ReachingForHandle.ProtestGestureAlpha == 0.0f);
	const SimCoreDriverPresentation::FExitSequencePose DoorOpening =
		SimCoreDriverPresentation::EvaluateExitSequence(0.1f);
	Ok &= TestTrue(TEXT("door opens before the driver crosses the sill"),
		DoorOpening.DoorOpenAlpha > 0.0f
		&& DoorOpening.DoorOpenAlpha < 1.0f
		&& DoorOpening.DriverExitAlpha == 0.0f);
	const SimCoreDriverPresentation::FExitSequencePose CrossingSill =
		SimCoreDriverPresentation::EvaluateExitSequence(0.5f);
	Ok &= TestTrue(TEXT("door remains fully open while the driver exits"),
		CrossingSill.DoorOpenAlpha == 1.0f
		&& CrossingSill.DriverExitAlpha > 0.0f
		&& CrossingSill.DriverExitAlpha < 1.0f
		&& CrossingSill.ProtestGestureAlpha == 0.0f);
	const SimCoreDriverPresentation::FExitSequencePose ClosingDoor =
		SimCoreDriverPresentation::EvaluateExitSequence(0.82f);
	Ok &= TestTrue(TEXT("driver closes the door only after both feet are outside"),
		ClosingDoor.DoorOpenAlpha > 0.0f
		&& ClosingDoor.DoorOpenAlpha < 1.0f
		&& ClosingDoor.DriverExitAlpha == 1.0f
		&& ClosingDoor.ProtestGestureAlpha == 0.0f);
	const SimCoreDriverPresentation::FExitSequencePose DoorClosed =
		SimCoreDriverPresentation::EvaluateExitSequence(0.92f);
	Ok &= TestTrue(TEXT("door is closed before the protest gesture begins"),
		DoorClosed.DoorOpenAlpha == 0.0f
		&& DoorClosed.DriverExitAlpha == 1.0f
		&& DoorClosed.ProtestGestureAlpha == 0.0f);
	const SimCoreDriverPresentation::FExitSequencePose Protest =
		SimCoreDriverPresentation::EvaluateExitSequence(1.0f);
	Ok &= TestTrue(TEXT("driver protests only after exiting and closing the door"),
		Protest.DoorOpenAlpha == 0.0f
		&& Protest.DriverExitAlpha == 1.0f
		&& Protest.ProtestGestureAlpha == 1.0f);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriverActorTest,
	"DriveIntegration.Presentation.Driver.SeatedAndSlumpedPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriverActorTest::RunTest(const FString& Parameters)
{
	FDriverTestWorld TestWorld;
	if (!TestNotNull(TEXT("world exists"), TestWorld.World)) return false;
	auto* Actor = TestWorld.World->SpawnActor<ASimCoreNpcPresentationActor>();
	if (!TestNotNull(TEXT("NPC exists"), Actor)) return false;
	auto* Driver = Actor->GetDriverPresentation();
	if (!TestNotNull(TEXT("shared driver component exists"), Driver)) return false;
	bool Ok = TestTrue(TEXT("Manny and authored steering wheel are present"),
		Driver->HasDriverAssets() && Driver->GetSteeringWheel() != nullptr);
	UStaticMesh* AuthoredDoor = LoadObject<UStaticMesh>(nullptr,
		SimCoreSedanVisualContract::DriverDoorObjectPath());
	Ok &= TestNotNull(TEXT("separate authored left-front door asset exists"),
		AuthoredDoor);
	Ok &= TestTrue(TEXT("hinge uses the authored curved door instead of a cube overlay"),
		Driver->GetDriverDoorPanel() != nullptr
		&& Driver->GetDriverDoorPanel()->GetStaticMesh() == AuthoredDoor
		&& Driver->GetDriverDoorPanel()->GetRelativeScale3D().Equals(
			FVector::OneVector, KINDA_SMALL_NUMBER));
	Ok &= TestNull(TEXT("real body aperture needs no fixed black mask"),
		Driver->GetDriverDoorAperture());
	Ok &= TestEqual(TEXT("front/rear seats, floor, console, door trim and instruments exist"),
		Driver->GetInteriorPanelCount(), 12);
	Ok &= TestEqual(TEXT("door preserves the authored body material-slot contract"),
		Driver->GetDriverDoorPanel()->GetNumMaterials(),
		Actor->GetBody()->GetNumMaterials());
	Ok &= TestEqual(TEXT("every authored door slot has an independent dent MID"),
		Driver->GetDriverDoorDamageMaterialCount(),
		Driver->GetDriverDoorPanel()->GetNumMaterials());
	SimCoreDamagePresentation::FZoneWeights DoorDent;
	DoorDent.Left = 0.625f;
	DoorDent.bContactLocal = true;
	DoorDent.Dents.Add({FVector2D(0.0, 1.0), FVector2D(0.0, -1.0), 0.8f, 0.2f});
	Driver->ApplyDoorDamage(DoorDent);
	float DoorLeftWeight = 1.0f;
	Ok &= TestTrue(TEXT("contact-local door damage disables broad WPO like the body"),
		Driver->GetDriverDoorPanel()->GetMaterial(0)
		&& Driver->GetDriverDoorPanel()->GetMaterial(0)->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("DentLeft")), DoorLeftWeight)
		&& DoorLeftWeight == 0.0f);
	Ok &= TestTrue(TEXT("contact-local dent patch deforms only the hinged door geometry"),
		Driver->GetDriverDoorDeformableBody()
		&& Driver->GetDriverDoorDeformableBody()->GetDeformedVertexCount() > 0
		&& Driver->GetDriverDoorDeformableBody()->IsVisible()
		&& !Driver->GetDriverDoorPanel()->IsVisible());
	Driver->ApplyDoorDamage({});
	Ok &= TestTrue(TEXT("door damage reset restores the authored hinged mesh"),
		Driver->GetDriverDoorPanel()->IsVisible()
		&& !Driver->GetDriverDoorDeformableBody()->IsVisible());

	auto State = NpcState();
	Ok &= TestTrue(TEXT("upright snapshot applies"),
		Actor->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector));
	UPoseableMeshComponent* Mesh = Driver->GetDriverMesh();
	if (!TestNotNull(TEXT("driver poseable mesh exists"), Mesh)) return false;
	const auto BoneInVehicle = [Mesh](const FName Bone)
	{
		return Mesh->GetRelativeTransform().TransformPosition(
			Mesh->GetBoneTransformByName(Bone,
				EBoneSpaces::ComponentSpace).GetLocation());
	};
	const FVector SeatedHeadVehicle = BoneInVehicle(TEXT("head"));
	const FVector SeatedPelvisVehicle = BoneInVehicle(TEXT("pelvis"));
	Ok &= TestTrue(TEXT("seated driver keeps visible roof clearance without entering the floor"),
		SeatedHeadVehicle.Z <= 92.0f && SeatedHeadVehicle.Z >= 70.0f
		&& SeatedPelvisVehicle.Z >= 20.0f);
	const FTransform WheelToVehicle = Driver->GetSteeringWheel()->GetRelativeTransform();
	const FVector LeftGrip = WheelToVehicle.TransformPosition(FVector(0.0, -14.2, 0.0));
	const FVector RightGrip = WheelToVehicle.TransformPosition(FVector(0.0, 14.2, 0.0));
	const FVector LeftHandVehicle = BoneInVehicle(TEXT("hand_l"));
	const FVector RightHandVehicle = BoneInVehicle(TEXT("hand_r"));
	const FVector LeftElbowVehicle = BoneInVehicle(TEXT("lowerarm_l"));
	const FVector RightElbowVehicle = BoneInVehicle(TEXT("lowerarm_r"));
	Ok &= TestTrue(TEXT("both hands use symmetric nine-and-three steering-wheel grips"),
		LeftHandVehicle.Equals(LeftGrip, 0.5f)
		&& RightHandVehicle.Equals(RightGrip, 0.5f)
		&& LeftHandVehicle.Y < RightHandVehicle.Y);
	Ok &= TestTrue(TEXT("elbows remain outside the torso and below the shoulders"),
		LeftElbowVehicle.Y < -36.0f && RightElbowVehicle.Y > -32.0f
		&& LeftElbowVehicle.Z < SeatedHeadVehicle.Z
		&& RightElbowVehicle.Z < SeatedHeadVehicle.Z);
	const FVector UprightHead = Mesh->GetBoneTransformByName(
		TEXT("head"), EBoneSpaces::ComponentSpace).GetLocation();

	State.CollisionEventSequence = 1;
	State.LastImpactImpulseNs = 9000.0f;
	State.DamagePercent = 70.0f;
	State.DamageZone = SimCoreProtocol::EVehicleDamageZone::Front;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Holding;
	Ok &= TestTrue(TEXT("damaged snapshot applies"),
		Actor->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector));
	Driver->AdvancePresentation(1.0f);
	const FVector InjuredHead = Mesh->GetBoneTransformByName(
		TEXT("head"), EBoneSpaces::ComponentSpace).GetLocation();
	Ok &= TestTrue(TEXT("impact blends driver head forward toward the wheel"),
		InjuredHead.Y > UprightHead.Y + 20.0f);
	Ok &= TestTrue(TEXT("impact drops driver head onto the wheel"),
		InjuredHead.Z < UprightHead.Z - 15.0f);
	Ok &= TestTrue(TEXT("injury blend reaches the authoritative target"),
		Driver->GetCurrentInjuryAlpha() > 0.75f);

	State = NpcState();
	State.CollisionEventSequence = 2;
	State.LastImpactImpulseNs = 3000.0f;
	State.DamagePercent = 10.0f;
	State.RuntimeRecoveryPhase = SimCoreProtocol::ERuntimeRecoveryPhase::Holding;
	Ok &= TestTrue(TEXT("moderate settled crash snapshot applies"),
		Actor->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector));
	Driver->AdvancePresentation(0.15f);
	Ok &= TestTrue(TEXT("driver door visibly opens before the driver moves"),
		Driver->GetCurrentDoorOpenAlpha() > 0.0f
		&& Driver->GetCurrentDriverExitAlpha() == 0.0f
		&& Driver->GetDriverDoorPivot() != nullptr
		&& Driver->GetDriverDoorPivot()->GetRelativeRotation().Yaw > 1.0f);
	Driver->AdvancePresentation(0.65f);
	Ok &= TestTrue(TEXT("open door remains staged while the driver crosses the sill"),
		Driver->GetCurrentDoorOpenAlpha() > 0.99f
		&& Driver->GetCurrentDriverExitAlpha() > 0.2f
		&& Driver->GetCurrentGestureAlpha() == 0.0f);
	Driver->AdvancePresentation(0.8f);
	Ok &= TestTrue(TEXT("uninjured NPC exits, closes the door and makes a one-hand gesture"),
		Driver->GetCurrentProtestAlpha() > 0.99f
		&& Driver->GetCurrentDriverExitAlpha() > 0.99f
		&& Driver->GetCurrentDoorOpenAlpha() < 0.01f
		&& Driver->GetCurrentGestureAlpha() > 0.99f
		&& Driver->GetDriverMesh()->GetRelativeLocation().Y < -100.0f);

	State = NpcState();
	State.PlaySessionId = TEXT("driver-reset");
	Ok &= TestTrue(TEXT("fresh reset snapshot applies"),
		Actor->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector));
	Driver->AdvancePresentation(1.6f);
	Ok &= TestTrue(TEXT("reset returns the driver to the normal seat pose"),
		Driver->GetCurrentInjuryAlpha() < 0.1f
		&& Driver->GetCurrentProtestAlpha() < 0.1f
		&& Driver->GetCurrentDoorOpenAlpha() < 0.01f
		&& Driver->GetCurrentDriverExitAlpha() < 0.01f);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreFleetCabinTest,
	"DriveIntegration.Presentation.Driver.FleetCabinVisibilityAndDoorDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreFleetCabinTest::RunTest(const FString& Parameters)
{
	using SimCoreProtocol::ERuntimeVehicleClass;
	FDriverTestWorld TestWorld;
	if (!TestNotNull(TEXT("world exists"), TestWorld.World)) return false;
	auto* Npc = TestWorld.World->SpawnActor<ASimCoreNpcPresentationActor>();
	auto* Player = TestWorld.World->SpawnActor<AExternalVehiclePawn>();
	if (!TestNotNull(TEXT("NPC"), Npc) || !TestNotNull(TEXT("player"), Player)) return false;
	USimCoreDriverPresentation* Driver = Npc->GetDriverPresentation();
	SimCoreDamagePresentation::FZoneWeights Dent;
	Dent.Left = 0.6f;
	Dent.bContactLocal = true;
	Dent.Dents.Add({FVector2D(0.0, 1.0), FVector2D(0.0, -1.0), 0.8f, 0.2f});
	bool Ok = true;
	for (const auto Class : {ERuntimeVehicleClass::Sedan, ERuntimeVehicleClass::Compact,
		ERuntimeVehicleClass::Truck, ERuntimeVehicleClass::Motorcycle, ERuntimeVehicleClass::Sedan})
	{
		auto State = NpcState();
		State.RuntimeVehicleClass = Class;
		Ok &= TestTrue(TEXT("class snapshot applies"),
			Npc->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector));
		Ok &= TestTrue(TEXT("every NPC class has a visible driver/rider"),
			Driver->HasDriverAssets() && Driver->GetDriverMesh()->IsVisible()
			&& !Driver->GetDriverMesh()->bOwnerNoSee);
		Ok &= TestTrue(TEXT("player class selection enables its matching cabin and driver"),
			Player->ConfigureVehicleClass(Class) && Player->DriverPresentation->HasDriverAssets()
			&& Player->DriverPresentation->GetDriverMesh()->IsVisible());
		Ok &= TestTrue(TEXT("player and NPC share the same seat placement"),
			Player->DriverPresentation->GetRelativeTransform().Equals(Driver->GetRelativeTransform(), 0.001f));
		const bool bRider = Class == ERuntimeVehicleClass::Motorcycle;
		if (bRider)
		{
			const auto BoneInBike = [Driver](const FName Bone)
			{
				auto* Mesh = Driver->GetDriverMesh();
				return Mesh->GetRelativeTransform().TransformPosition(
					Mesh->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace).GetLocation());
			};
			const double LeftError = FVector::Distance(BoneInBike(TEXT("hand_l")), FVector(75.0, -40.0, 70.0));
			const double RightError = FVector::Distance(BoneInBike(TEXT("hand_r")), FVector(75.0, 40.0, 70.0));
			Ok &= TestTrue(*FString::Printf(TEXT("rider reaches existing handlebar grips (%.1f/%.1f cm)"),
				LeftError, RightError), LeftError < 15.0 && RightError < 15.0);
		}
		if (Class == ERuntimeVehicleClass::Truck)
		{
			auto* Mesh = Driver->GetDriverMesh();
			const FVector Head = Driver->GetRelativeTransform().TransformPosition(
				Mesh->GetRelativeTransform().TransformPosition(Mesh->GetBoneTransformByName(
					TEXT("head"), EBoneSpaces::ComponentSpace).GetLocation()));
			Ok &= TestTrue(TEXT("truck driver's head is inside the open cab, below its roof"),
				Head.X > 32.0 && Head.X < 171.0 && Head.Z > 98.0 && Head.Z < 158.0);
		}
		Ok &= TestEqual(TEXT("only cars show the cabin steering wheel"),
			Driver->GetSteeringWheel()->IsVisible(), !bRider);
		TArray<USceneComponent*> Parts;
		Driver->GetChildrenComponents(true, Parts);
		int32 VisibleSeats = 0;
		for (const USceneComponent* Part : Parts)
		{
			if (Part->GetName().Contains(TEXT("Seat")) && Part->IsVisible()) ++VisibleSeats;
			if (bRider && Part->GetName().Contains(TEXT("Door")))
			{
				Ok &= TestFalse(TEXT("bike has no visible sedan door part"), Part->IsVisible());
			}
		}
		Ok &= TestTrue(TEXT("car seats restored; bike uses its existing saddle"),
			bRider ? VisibleSeats == 0 : VisibleSeats >= 4);
		for (const auto& Damage : {Dent, SimCoreDamagePresentation::FZoneWeights{}})
		{
			Driver->ApplyDoorDamage(Damage);
			if (Class != ERuntimeVehicleClass::Sedan)
			{
				Ok &= TestFalse(TEXT("fleet damage/reset cannot create a sedan door"),
					Driver->GetDriverDoorPanel()->IsVisible()
					|| Driver->GetDriverDoorDeformableBody()->IsVisible());
			}
		}
	}
	Ok &= TestTrue(TEXT("player switches to motorcycle"),
		Player->ConfigureVehicleClass(ERuntimeVehicleClass::Motorcycle));
	for (const auto& Damage : {Dent, SimCoreDamagePresentation::FZoneWeights{}})
	{
		Player->DriverPresentation->ApplyDoorDamage(Damage);
		Ok &= TestFalse(TEXT("player bike door stays hidden after damage and reset"),
			Player->DriverPresentation->GetDriverDoorPanel()->IsVisible()
			|| Player->DriverPresentation->GetDriverDoorDeformableBody()->IsVisible());
	}
	Ok &= TestTrue(TEXT("switching player back restores a closed sedan door"),
		Player->ConfigureVehicleClass(ERuntimeVehicleClass::Sedan)
			&& Player->DriverPresentation->GetDriverDoorPanel()->IsVisible()
			&& !Player->DriverPresentation->GetDriverDoorDeformableBody()->IsVisible());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreMotorcycleRiderEjectionTest,
	"DriveIntegration.Presentation.Driver.MotorcycleEjectionAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreMotorcycleRiderEjectionTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	FDriverTestWorld TestWorld(true);
	if (!TestNotNull(TEXT("world exists"), TestWorld.World)) return false;
	auto* Floor = TestWorld.World->SpawnActor<AActor>();
	auto* Ground = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Ground);
	Ground->SetBoxExtent(FVector(5000, 5000, 25));
	Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ground->SetCollisionObjectType(ECC_WorldStatic);
	Ground->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->AddInstanceComponent(Ground);
	Ground->RegisterComponent();
	Floor->SetActorLocation(FVector(0, 0, -25));
	auto* Npc = TestWorld.World->SpawnActor<ASimCoreNpcPresentationActor>();
	if (!TestNotNull(TEXT("rider owner"), Npc)) return false;
	Npc->SetActorLocation(FVector(0, 0, 100));
	auto* Driver = Npc->GetDriverPresentation();
	Driver->ConfigureVehicleClass(ERuntimeVehicleClass::Motorcycle);
	Driver->SetDriverViewActive(true);
	auto State = NpcState();
	State.RuntimeVehicleClass = ERuntimeVehicleClass::Motorcycle;
	State.SpeedMps = 12.0f;
	State.LinearVelocityEnu = FVector3d(0, 12, 0);
	State.SimulationTimeNs = 1000000000;
	Driver->ApplyAuthoritativeState(State);
	State.CollisionEventSequence = 1;
	State.LastImpactImpulseNs = 120.0f;
	bool Ok = TestFalse(TEXT("a gentle contact keeps the rider seated"),
		SimCoreDriverPresentation::ShouldEjectRider(State, 12.0f));
	State.LastImpactImpulseNs = 1500.0f;
	State.SpeedMps = 0.0f;
	State.LinearVelocityEnu = FVector3d::ZeroVector;
	State.SimulationTimeNs += 50000000;
	Driver->ApplyAuthoritativeState(State);
	Ok &= TestTrue(TEXT("hard collision separates the rider with pre-impact momentum"),
		Driver->IsRiderEjected() && Driver->GetRiderVelocityCmPerSecond().X > 1000.0
		&& !Driver->GetDriverMesh()->bOwnerNoSee);
	const FVector LaunchPelvis = Driver->GetDriverMesh()->GetBoneLocation(TEXT("pelvis"));
	const double LaunchVerticalVelocity = Driver->GetRiderVelocityCmPerSecond().Z;
	auto* Bridge = TestWorld.World->SpawnActor<AActor>();
	auto* BridgeDeck = NewObject<UBoxComponent>(Bridge);
	Bridge->SetRootComponent(BridgeDeck);
	BridgeDeck->SetBoxExtent(FVector(5000, 5000, 10));
	BridgeDeck->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BridgeDeck->SetCollisionObjectType(ECC_WorldStatic);
	BridgeDeck->SetCollisionResponseToAllChannels(ECR_Block);
	Bridge->AddInstanceComponent(BridgeDeck);
	BridgeDeck->RegisterComponent();
	Bridge->SetActorLocation(FVector(0, 0, LaunchPelvis.Z + 140.0));
	Npc->SetActorLocation(FVector(-1000, 0, 100));
	State.SimulationTimeNs += 100000000;
	Driver->ApplyAuthoritativeState(State);
	const FVector AirbornePelvis = Driver->GetDriverMesh()->GetBoneLocation(TEXT("pelvis"));
	Ok &= TestTrue(TEXT("rider continues in world space instead of following the bike"),
		AirbornePelvis.X > LaunchPelvis.X + 100.0);
	Ok &= TestTrue(TEXT("airborne velocity loses approximately 9.81 m/s each second"),
		FMath::IsNearlyEqual(Driver->GetRiderVelocityCmPerSecond().Z,
			LaunchVerticalVelocity - 98.1, 0.5));
	Ok &= TestTrue(TEXT("overhead bridge cannot snap the airborne rider onto its deck"),
		!Driver->IsRiderGrounded() && AirbornePelvis.Z < LaunchPelvis.Z + 20.0);
	Driver->AdvancePresentation(1.0f);
	Driver->ApplyAuthoritativeState(State);
	Ok &= TestTrue(TEXT("paused simulation clock freezes the detached rider"),
		Driver->GetDriverMesh()->GetBoneLocation(TEXT("pelvis")).Equals(AirbornePelvis, 0.01));
	State.ServerHealth.bPresent = true;
	State.ServerHealth.Status = EServerHealthStatus::SafeStop;
	State.SimulationTimeNs += 2000000000;
	Driver->ApplyAuthoritativeState(State);
	Ok &= TestTrue(TEXT("SafeStop freezes the rider even while the host clock advances"),
		Driver->GetDriverMesh()->GetBoneLocation(TEXT("pelvis")).Equals(AirbornePelvis, 0.01));
	State.ServerHealth.Status = EServerHealthStatus::Active;
	for (int32 Step = 0; Step < 100; ++Step)
	{
		State.SimulationTimeNs += 50000000;
		Driver->ApplyAuthoritativeState(State);
	}
	Ok &= TestTrue(TEXT("rider lands and ground friction stops sliding"),
		Driver->IsRiderGrounded() && Driver->GetRiderVelocityCmPerSecond().Size() < 1.0);
	double LowestBone = TNumericLimits<double>::Max();
	for (const FName Bone : {FName(TEXT("head")), FName(TEXT("hand_l")), FName(TEXT("hand_r")),
		FName(TEXT("foot_l")), FName(TEXT("foot_r")), FName(TEXT("pelvis"))})
		LowestBone = FMath::Min(LowestBone, Driver->GetDriverMesh()->GetBoneLocation(Bone).Z);
	Ok &= TestTrue(TEXT("settled limbs remain on the ground, not buried or floating"),
		LowestBone >= 9.0 && LowestBone <= 25.0);
	State.PlaySessionId = TEXT("rider-new-play");
	State.SimulationTimeNs = 0;
	State.CollisionEventSequence = 0;
	Driver->ApplyAuthoritativeState(State);
	Ok &= TestTrue(TEXT("new Play restores a seated rider on the motorcycle"),
		!Driver->IsRiderEjected() && !Driver->IsRiderGrounded()
		&& !Driver->GetDriverMesh()->IsUsingAbsoluteLocation()
		&& Driver->GetDriverMesh()->bOwnerNoSee);
	State.CollisionEventSequence = 1;
	State.LastImpactImpulseNs = 300.0f;
	Driver->ApplyAuthoritativeState(State);
	Ok &= TestFalse(TEXT("a low-speed side contact initially stays seated"), Driver->IsRiderEjected());
	State.RollDegrees = 80.0f;
	State.SimulationTimeNs += 50000000;
	Driver->ApplyAuthoritativeState(State);
	Ok &= TestTrue(TEXT("later rollover ejects even without a second collision event"), Driver->IsRiderEjected());
	Driver->ConfigureVehicleClass(ERuntimeVehicleClass::Sedan);
	Ok &= TestTrue(TEXT("changing vehicle class clears a detached motorcycle rider"),
		!Driver->IsRiderEjected() && !Driver->GetDriverMesh()->IsUsingAbsoluteLocation());
	return Ok;
}

#endif
