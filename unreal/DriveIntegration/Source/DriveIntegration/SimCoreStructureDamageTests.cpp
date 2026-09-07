#include "SimCoreStructureDamageActor.h"
#include "SimCoreTrafficSignalActor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "SimCoreCoordinateFrames.h"
#include <limits>

namespace
{
using namespace SimCoreProtocol;

FStructureState Building()
{
	FStructureState State;
	State.ColliderId = TEXT("building_damage_qa");
	State.Kind = EStructureKind::Building;
	State.DamagePercent = 45.0f;
	State.EventSequence = 3;
	State.ImpactPointEnu = FVector3d(8.0, 15.0, 1.0);
	State.ImpactNormalEnu = FVector3d(-1.0, 0.0, 0.0);
	State.ImpactHalfWidthMeters = 1.0f;
	State.ImpactHalfHeightMeters = 0.6f;
	State.ImpactSeverity = 0.45f;
	return State;
}

FStructureState Pole()
{
	FStructureState State = Building();
	State.ColliderId = TEXT("signal_pole_1");
	State.Kind = EStructureKind::SignalPole;
	State.ImpactHalfWidthMeters = 0.0f;
	State.ImpactHalfHeightMeters = 0.0f;
	State.ImpactSeverity = 0.0f;
	State.SignalId = 1;
	State.BasePositionEnu = FVector3d(8.0, 15.0, 0.0);
	State.FallDirectionEnu = FVector3d(1.0, 0.0, 0.0);
	State.FallAngleRadians = 0.7f;
	State.bDisabled = true;
	return State;
}

FTrafficSignalState Signal()
{
	FTrafficSignalState State;
	State.SignalId = 1;
	State.GroupId = 1;
	State.ControllerId = 1;
	State.Aspect = ETrafficSignalAspect::Red;
	State.PositionEnu = FVector3d(8.0, 15.0, 0.0);
	State.RemainingSeconds = 0.0f;
	return State;
}

struct FDamageQaWorld
{
	UWorld* World = nullptr;
	explicit FDamageQaWorld(EWorldType::Type Type = EWorldType::Game)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
			.SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(Type, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreStructureQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
		if (World && GEngine) GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
	}
	~FDamageQaWorld()
	{
		if (World)
		{
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreStructurePoleTransformTest,
	"DriveIntegration.StructureDamage.PoleGeometryAndOutage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreStructurePoleTransformTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	const FVector Offset(100.0, -50.0, 20.0);
	for (const FVector3d& Direction : {FVector3d(1, 0, 0), FVector3d(-1, 0, 0),
		FVector3d(0, 1, 0), FVector3d(0, -1, 0)})
	{
		for (double Heading : {-UE_DOUBLE_PI, -UE_DOUBLE_PI / 2.0, 0.0, UE_DOUBLE_PI / 2.0})
		{
			auto State = Pole(); State.FallDirectionEnu = Direction; State.HeadingRadians = Heading;
			const FTransform Transform = SimCoreTrafficSignals::BuildDamagedPoleTransform(State, Offset);
			const FVector TargetUp = FVector::UpVector * FMath::Cos(double(State.FallAngleRadians))
				+ FVector(SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(Direction)) * FMath::Sin(double(State.FallAngleRadians));
			Ok &= TestTrue(TEXT("pole tilts toward server ENU direction independent of heading"),
				Transform.GetUnitAxis(EAxis::Z).Equals(TargetUp, 0.00001));
			Ok &= TestTrue(TEXT("ground hinge includes server half-width support lift"),
				Transform.GetLocation().Equals(FVector(1500, 800, 25.0 * FMath::Sin(double(State.FallAngleRadians))) + Offset, 0.001));
			State.FallAngleRadians = 0.0f;
			const FTransform Upright = SimCoreTrafficSignals::BuildDamagedPoleTransform(State, Offset);
			Ok &= TestTrue(TEXT("intact heading remains North-zero clockwise-positive"),
				Upright.GetUnitAxis(EAxis::X).Equals(FVector(FMath::Cos(Heading), FMath::Sin(Heading), 0), 0.00001));
		}
	}
	FDamageQaWorld Scene;
	if (!TestNotNull(TEXT("isolated structure game world"), Scene.World)) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCoreTrafficSignalActor>();
	if (!TestNotNull(TEXT("damage-capable signal actor"), Actor)) return false;
	auto Broken = Signal(); Broken.bOutOfService = true;
	Actor->ApplyAuthoritativeSignal(Broken, true, true, 0.0, Offset);
	Actor->ApplyStructureDamage(Pole(), Offset);
	const FTransform Fallen = Actor->GetActorTransform();
	const auto& Display = Actor->GetDisplayState();
	Ok &= TestTrue(TEXT("broken lamps are all off and labelled BROKEN"),
		Display.bOutOfService && !Display.bRed && !Display.bYellow && !Display.bGreen
		&& Actor->GetStatusText().Contains(TEXT("BROKEN")));
	Actor->ApplyAuthoritativeSignal(Signal(), false, false, 1.0, Offset);
	Actor->SetFailSafe();
	Ok &= TestTrue(TEXT("stale/invalidation never stands the pole back up or lights a broken lamp"),
		Actor->GetActorTransform().Equals(Fallen, 0.00001) && Actor->GetDisplayState().bOutOfService
		&& !Actor->GetDisplayState().bGreen && !Actor->GetDisplayState().bRed);
	Actor->ClearStructureDamage();
	Actor->ApplyAuthoritativeSignal(Signal(), true, true, 0.0, Offset);
	Ok &= TestTrue(TEXT("new accepted intact state explicitly restores base geometry and red lamp"),
		Actor->GetActorUpVector().Equals(FVector::UpVector, 0.00001)
		&& Actor->GetDisplayState().bRed && !Actor->GetDisplayState().bOutOfService);
	Ok &= TestFalse(TEXT("pole presentation does not add Unreal physical authority"), Actor->GetActorEnableCollision());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreStructureFacadeTest,
	"DriveIntegration.StructureDamage.FacadeIdempotenceAndBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreStructureFacadeTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	FDamageQaWorld Scene;
	if (!TestNotNull(TEXT("isolated facade game world"), Scene.World)) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCoreStructureDamageActor>();
	if (!TestNotNull(TEXT("transient facade actor"), Actor)) return false;
	auto State = Building();
	Actor->ApplyAuthoritativeDamage(State);
	const int32 Count = Actor->GetDebrisCount();
	Ok &= TestTrue(TEXT("contact creates bounded cracks and fragments"), Actor->GetCrackCount() > 0
		&& Actor->GetCrackCount() <= 32 && Count >= 10 && Count <= SimCoreStructureDamage::MaxDebrisFragments);
	const FVector Normal(SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(State.ImpactNormalEnu));
	const FVector Point = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(State.ImpactPointEnu, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("marks sit outside facade and align to contact normal"),
		Actor->GetActorLocation().Equals(Point + Normal * 1.5, 0.001)
		&& Actor->GetActorUpVector().Equals(Normal, 0.00001));
	for (int32 Tick = 0; Tick < 120; ++Tick) Actor->ApplyAuthoritativeDamage(State);
	Ok &= TestTrue(TEXT("60Hz retransmission cannot repeatedly spawn impact debris"),
		Actor->GetPresentedEventCount() == 1 && Actor->GetDebrisCount() == Count && Actor->GetLastEventSequence() == 3);
	State.EventSequence = 2; Actor->ApplyAuthoritativeDamage(State);
	Ok &= TestEqual(TEXT("older events cannot replace current damage"), Actor->GetPresentedEventCount(), 1u);
	State.EventSequence = 4; State.DamagePercent = 90.0f; Actor->ApplyAuthoritativeDamage(State);
	Ok &= TestTrue(TEXT("new event refreshes a bounded pool, not unbounded components"),
		Actor->GetPresentedEventCount() == 2 && Actor->GetDebrisCount() <= SimCoreStructureDamage::MaxDebrisFragments);
	TArray<UPrimitiveComponent*> Meshes; Actor->GetComponents(Meshes);
	Ok &= TestTrue(TEXT("only two instanced visual components are allocated"), Meshes.Num() == 2 && !Actor->GetActorEnableCollision());
	for (UPrimitiveComponent* Mesh : Meshes)
	{
		Ok &= TestTrue(TEXT("facade and debris have no collision, overlap, physics or navigation"),
			Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Mesh->GetGenerateOverlapEvents()
			&& !Mesh->IsSimulatingPhysics() && !Mesh->CanEverAffectNavigation());
	}
	Actor->Tick(SimCoreStructureDamage::DebrisLifetimeSeconds + 0.1f);
	Ok &= TestTrue(TEXT("debris expires but facade damage remains until authoritative reset"),
		Actor->GetDebrisCount() == 0 && Actor->GetCrackCount() > 0 && !Actor->IsActorTickEnabled());
	Actor->ApplyAuthoritativeDamage(State);
	Ok &= TestEqual(TEXT("same event cannot resurrect expired debris"), Actor->GetDebrisCount(), 0);
	Actor->Destroy();
	Ok &= TestTrue(TEXT("cleanup destroys ephemeral damage"), Actor->IsActorBeingDestroyed());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreStructureLocalPatchTest,
	"DriveIntegration.StructureDamage.LocalFootprintHistoryAndVisibleDebris",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreStructureLocalPatchTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	FDamageQaWorld Scene;
	if (!TestNotNull(TEXT("local patch QA world"), Scene.World)) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCoreStructureDamageActor>();
	if (!TestNotNull(TEXT("local patch actor"), Actor)) return false;
	auto State = Building();
	State.ImpactSeverity = 0.2f;
	Actor->ApplyAuthoritativeDamage(State);
	const FVector First = Actor->GetDamageMarkLocation(0);
	const FVector2D FrontalExtent = Actor->GetDamageMarkHalfExtentCm(0);
	Ok &= TestTrue(TEXT("wire bumper width controls footprint independently of cumulative damage"),
		FrontalExtent.Equals(FVector2D(80.0, 33.6), 0.01));
	FTransform FragmentStart;
	Ok &= TestTrue(TEXT("at least ten visible concrete chips are instanced immediately"),
		Actor->GetDebrisCount() >= 10 && Actor->GetDebrisWorldTransform(0, FragmentStart)
		&& FragmentStart.GetScale3D().X >= 0.07 && FragmentStart.GetScale3D().Y >= 0.055);
	const FVector Normal(SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(State.ImpactNormalEnu));
	Ok &= TestTrue(TEXT("chips start outside the wall instead of inside its thickness"),
		FVector::DotProduct(FragmentStart.GetLocation() - First, Normal) >= 11.9);
	Actor->Tick(0.1f);
	FTransform FragmentMoved;
	Actor->GetDebrisWorldTransform(0, FragmentMoved);
	Ok &= TestTrue(TEXT("visible burst travels outward and upward from the impact"),
		FVector::DotProduct(FragmentMoved.GetLocation() - FragmentStart.GetLocation(), Normal) > 15.0
		&& FragmentMoved.GetLocation().Z > FragmentStart.GetLocation().Z + 10.0);

	State.EventSequence = 4;
	State.ImpactPointEnu = FVector3d(18.0, 20.0, 1.0);
	State.ImpactNormalEnu = FVector3d(1.0, 0.0, 0.0);
	State.ImpactHalfWidthMeters = 0.1f;
	Actor->ApplyAuthoritativeDamage(State);
	const FVector Corner = Actor->GetDamageMarkLocation(1);
	const int32 DebrisCount = Actor->GetDebrisCount();
	Ok &= TestTrue(TEXT("opposite facade impact preserves the first scar at its original world location"),
		Actor->GetDamageMarkCount() == 2 && Actor->GetDamageMarkLocation(0).Equals(First, 0.001)
		&& !Corner.Equals(First, 1.0));
	Ok &= TestTrue(TEXT("glancing corner stamp is narrower than a full-width bumper at equal severity"),
		Actor->GetDamageMarkHalfExtentCm(1).X < FrontalExtent.X * 0.2);
	State.ImpactPointEnu += FVector3d(2.0, 2.0, 0.0);
	State.DamagePercent = 80.0f;
	State.ImpactSeverity = 0.8f;
	for (int32 Index = 0; Index < 120; ++Index) Actor->ApplyAuthoritativeDamage(State);
	Ok &= TestTrue(TEXT("one episode may grow its scar but cannot slide scars or recreate bursts at 60Hz"),
		Actor->GetDamageMarkLocation(0).Equals(First, 0.001)
		&& Actor->GetDamageMarkLocation(1).Equals(Corner, 0.001)
		&& Actor->GetPresentedEventCount() == 2 && Actor->GetDebrisCount() == DebrisCount);
	for (int32 Index = 0; Index < 20; ++Index)
	{
		State.EventSequence = 5 + Index;
		State.ImpactPointEnu.Y += 2.0;
		Actor->ApplyAuthoritativeDamage(State);
	}
	Ok &= TestTrue(TEXT("many separate wall hits retain the newest eight scars within a fixed ISM budget"),
		Actor->GetDamageMarkCount() == SimCoreStructureDamage::MaxDamageMarks
		&& Actor->GetCrackCount() <= SimCoreStructureDamage::MaxDamageMarks * SimCoreStructureDamage::MaxCracksPerMark
		&& Actor->GetDebrisCount() <= SimCoreStructureDamage::MaxDebrisFragments);
	const FVector LastBeforeRebase = Actor->GetDamageMarkLocation(7);
	const FVector Rebase(100, -200, 50);
	Actor->ApplyAuthoritativeDamage(State, Rebase);
	Ok &= TestTrue(TEXT("only explicit presentation-origin changes may translate accumulated scars"),
		Actor->GetDamageMarkLocation(7).Equals(LastBeforeRebase + Rebase, 0.001));
	for (int32 Index = 0; Index < 28; ++Index)
	{
		Actor->Tick(0.1f);
		for (int32 Fragment = 0; Fragment < Actor->GetDebrisCount(); ++Fragment)
		{
			FTransform Current;
			Actor->GetDebrisWorldTransform(Fragment, Current);
			Ok &= TestTrue(TEXT("chips bounce/rest above the floor instead of disappearing underground"),
				Current.GetLocation().Z >= Rebase.Z + 3.99);
		}
	}
	Ok &= TestTrue(TEXT("visible chip budget persists beyond the old 1.8-second lifetime"), Actor->GetDebrisCount() > 0);
	Actor->Tick(0.5f);
	Ok &= TestTrue(TEXT("bounded debris expires independently of persistent facade marks"),
		Actor->GetDebrisCount() == 0 && Actor->GetDamageMarkCount() == SimCoreStructureDamage::MaxDamageMarks);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreStructureInvalidPresentationTest,
	"DriveIntegration.StructureDamage.InvalidInputAndPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreStructureInvalidPresentationTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	FTransform Frame;
	auto State = Building();
	State.ImpactNormalEnu = FVector3d::ZeroVector;
	Ok &= TestFalse(TEXT("degenerate contact normal is rejected"),
		SimCoreStructureDamage::BuildImpactTransform(State, FVector::ZeroVector, Frame));
	State = Building(); State.ImpactPointEnu.X = std::numeric_limits<double>::infinity();
	Ok &= TestFalse(TEXT("nonfinite contact cannot produce geometry"),
		SimCoreStructureDamage::BuildImpactTransform(State, FVector::ZeroVector, Frame));
	State = Building(); State.DamagePercent = 0.0f;
	Ok &= TestFalse(TEXT("undamaged structure produces no marks"),
		SimCoreStructureDamage::BuildImpactTransform(State, FVector::ZeroVector, Frame));
	Ok &= TestFalse(TEXT("pole state cannot create building damage"),
		SimCoreStructureDamage::BuildImpactTransform(Pole(), FVector::ZeroVector, Frame));
	FDamageQaWorld Scene(EWorldType::EditorPreview);
	if (!TestNotNull(TEXT("preview world"), Scene.World)) return false;
	auto* Actor = Scene.World->SpawnActor<ASimCoreStructureDamageActor>();
	if (!TestNotNull(TEXT("preview damage actor"), Actor)) return false;
	Actor->ApplyAuthoritativeDamage(Building());
	Ok &= TestTrue(TEXT("editor preview keeps runtime damage hidden"), Actor->IsHidden());
	Ok &= TestEqual(TEXT("editor preview cannot accept impact events"), Actor->GetPresentedEventCount(), 0u);
	Ok &= TestEqual(TEXT("editor preview cannot spawn particles"), Actor->GetDebrisCount(), 0);
	return Ok;
}

#endif
