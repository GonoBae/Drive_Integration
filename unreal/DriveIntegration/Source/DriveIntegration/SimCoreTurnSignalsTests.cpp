#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreTurnSignals.h"
#include "SimCoreVehicleAudio.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreSedanVisualContract.h"
#include "SimCoreSuspensionPresentation.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FSignalTestWorld
{
	UWorld* World = nullptr;
	FSignalTestWorld()
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
			.SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TurnSignalQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	}
	~FSignalTestWorld() { if (World) World->DestroyWorld(false); }
};

UMaterialInstanceDynamic* FindSignalMaterial(UMeshComponent* Body)
{
	for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
	{
		auto* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Index));
		if (Material && Material->GetMaterial()->GetFName() == SimCoreSedanVisualContract::SignalMaterialName) return Material;
	}
	return nullptr;
}

bool HasSignalValues(UMaterialInstanceDynamic* Material, float Left, float Right)
{
	float ActualLeft = -1, ActualRight = -1;
	return Material
		&& Material->GetScalarParameterValue(FMaterialParameterInfo(SimCoreSedanVisualContract::SignalLeftParameter), ActualLeft)
		&& Material->GetScalarParameterValue(FMaterialParameterInfo(SimCoreSedanVisualContract::SignalRightParameter), ActualRight)
		&& ActualLeft == Left && ActualRight == Right;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreAuthoredTurnSignalTest,
	"DriveIntegration.Presentation.TurnSignals.AuthoredLensesAndDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreAuthoredTurnSignalTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	FSignalTestWorld TestWorld;
	if (!TestNotNull(TEXT("world exists"), TestWorld.World)) return false;
	auto* Actor = TestWorld.World->SpawnActor<ASimCoreNpcPresentationActor>();
	if (!TestNotNull(TEXT("NPC exists"), Actor)) return false;
	auto* Signals = Actor->FindComponentByClass<USimCoreTurnSignals>();
	if (!TestNotNull(TEXT("signals are bound"), Signals)) return false;
	FVehicleState State;
	State.EntityId = 1001; State.EntityKind = EEntityKind::NpcVehicle;
	State.PlaySessionId = TEXT("signal-qa");
	State.PositionEnu = FVector3d(10,20,.85);
	State.CollisionHalfLengthMeters = 2.2f; State.CollisionHalfWidthMeters = 1;
	State.CollisionHalfHeightMeters = .75f; State.TurnIndicator = ETurnIndicator::Left;
	State.SimulationTimeNs = 695000000;
	bool Ok = TestTrue(TEXT("real NPC snapshot applies"), Actor->ApplySnapshot(State,0,0,true,.15f,FVector::ZeroVector));
	auto* SourceMaterial = FindSignalMaterial(Actor->GetBody());
	Ok &= TestTrue(TEXT("authored source lens blinks left independently"), HasSignalValues(SourceMaterial,1,0));
	TArray<UStaticMeshComponent*> Meshes;
	Actor->GetComponents(Meshes);
	int32 BodyWheelCabinMeshCount = 0;
	int32 SuspensionMeshCount = 0;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Cast<USimCoreSuspensionPresentation>(Mesh->GetAttachParent()))
		{
			++SuspensionMeshCount;
			Ok &= TestTrue(TEXT("added frame meshes have no collision, overlap or physics authority"),
				Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				&& !Mesh->GetGenerateOverlapEvents() && !Mesh->IsSimulatingPhysics());
		}
		else
		{
			++BodyWheelCabinMeshCount;
		}
	}
	Ok &= TestEqual(TEXT("body, wheels and the authored cabin interior exist"),
		BodyWheelCabinMeshCount, 17);
	Ok &= TestTrue(TEXT("suspension is a separate presentation group, not an extra lamp mesh"),
		SuspensionMeshCount > 0);
	TArray<UPointLightComponent*> Lights;
	Actor->GetComponents(Lights);
	Ok &= TestEqual(TEXT("at most four actual local lamps"), Lights.Num(),4);
	for (auto* Light : Lights)
	{
		const FVector Position = Light->GetRelativeLocation();
		const FVector Rest = SimCoreSedanVisualContract::TurnSignalLensPointCm(Position.X > 0,Position.Y < 0,.5,.5);
		Ok &= TestTrue(TEXT("local light is located at its real lens"), Position.Equals(Rest+FVector(0,0,2),.01));
		Ok &= TestTrue(TEXT("local illumination is daylight-readable, bounded and shadowless"),
			!Light->CastShadows && Light->Intensity >= 3000.0f
			&& Light->AttenuationRadius >= 400.0f
			&& Light->AttenuationRadius <= 500.0f
			&& Light->MaxDrawDistance <= 6000.0f
			&& Light->VolumetricScatteringIntensity >= 0.2f);
		Ok &= TestEqual(TEXT("only selected left lamps illuminate"), Light->IsVisible(),Position.Y < 0);
	}
	const TArray<FVector> CompactProfile = {
		FVector(164.0, -72.0, 42.0), FVector(164.0, 72.0, 42.0),
		FVector(-182.0, -72.0, 45.0), FVector(-182.0, 72.0, 45.0)};
	Ok &= TestTrue(TEXT("a four-lamp vehicle archetype profile is accepted"),
		Signals->SetLampPositions(CompactProfile));
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		Ok &= TestTrue(TEXT("model swap moves every lamp to its new authored lens"),
			Lights[Index]->GetRelativeLocation().Equals(
				CompactProfile[Index] + FVector(0,0,2), 0.01));
	}
	const TArray<FVector> IncompleteProfile = {FVector::ZeroVector};
	Ok &= TestFalse(TEXT("a partial model profile cannot leave missing lamps"),
		Signals->SetLampPositions(IncompleteProfile));
	Ok &= TestTrue(TEXT("rejected profile preserves the last complete lamp layout"),
		Lights[3]->GetRelativeLocation().Equals(
			CompactProfile[3] + FVector(0,0,2), 0.01));
	TArray<FVector> SedanProfile;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		SedanProfile.Add(SimCoreSedanVisualContract::TurnSignalLensPointCm(
			Index < 2, Index % 2 == 0, 0.5, 0.5));
	}
	Ok &= TestTrue(TEXT("sedan default can be restored after a vehicle model swap"),
		Signals->SetLampPositions(SedanProfile));
	Signals->UpdateSignal(ETurnIndicator::Left,1.195,false);
	Ok &= TestTrue(TEXT("blink off does not leave emissive amber on"), HasSignalValues(SourceMaterial,0,0));
	Signals->UpdateSignal(ETurnIndicator::Right,.1,false);
	Ok &= TestTrue(TEXT("opposite selection remains independent"), HasSignalValues(SourceMaterial,0,1));
	Signals->UpdateSignal(ETurnIndicator::Off,.1,true);
	Ok &= TestTrue(TEXT("hazards illuminate both real lenses"), HasSignalValues(SourceMaterial,1,1));

	State.CollisionEventSequence = 1; State.DamagePercent = 35;
	State.DamageZone = EVehicleDamageZone::Front;
	State.DentPatches.Add({FVector2D(1,.55),FVector2D(-1,0),.7f,.22f});
	Ok &= TestTrue(TEXT("contact-damaged snapshot applies"), Actor->ApplySnapshot(State,0,0,true,.15f,FVector::ZeroVector));
	auto* DeformedMaterial = FindSignalMaterial(Actor->GetDeformableBody());
	Ok &= TestTrue(TEXT("actual dent mesh is visible"), Actor->GetDeformableBody()->GetDeformedVertexCount() > 0);
	Ok &= TestTrue(TEXT("source and deformed body retain separate damage MIDs"), SourceMaterial && DeformedMaterial && SourceMaterial != DeformedMaterial);
	Ok &= TestTrue(TEXT("damage does not stop the existing left signal"), HasSignalValues(SourceMaterial,1,0) && HasSignalValues(DeformedMaterial,1,0));
	Signals->UpdateSignal(ETurnIndicator::Right,.1,false);
	Ok &= TestTrue(TEXT("both source and visible deformed material update"), HasSignalValues(SourceMaterial,0,1) && HasSignalValues(DeformedMaterial,0,1));
	SimCoreDamagePresentation::FAccumulator Accumulator;
	SimCoreDamagePresentation::Advance(Accumulator,State);
	bool AnyLightMoved = false;
	for (auto* Light : Lights)
	{
		const FVector Relative = Light->GetRelativeLocation();
		const FVector Rest = SimCoreSedanVisualContract::TurnSignalLensPointCm(Relative.X > 0,Relative.Y < 0,.5,.5);
		const FVector Deformed = USimCoreDeformableBody::DeformVertex(Rest,Accumulator.Weights);
		const FVector Expected = Actor->GetBody()->GetComponentTransform().TransformPosition(Deformed+FVector(0,0,2));
		Ok &= TestTrue(TEXT("light follows exactly the contact-deformed lens"), Light->GetComponentLocation().Equals(Expected,.01));
		AnyLightMoved |= !Deformed.Equals(Rest,.1);
	}
	Ok &= TestTrue(TEXT("front impact actually moves a lens/light"), AnyLightMoved);
	Signals->UpdateSignal(ETurnIndicator::Off,.1,false);
	Ok &= TestTrue(TEXT("reset can extinguish both visible bodies"), HasSignalValues(SourceMaterial,0,0) && HasSignalValues(DeformedMaterial,0,0));
	for (auto* Light : Lights) Ok &= TestFalse(TEXT("reset extinguishes local light"),Light->IsVisible());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSelectedTurnSignalPhaseTest,
	"DriveIntegration.Presentation.TurnSignals.SelectionRelativeFullCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreSelectedTurnSignalPhaseTest::RunTest(const FString& Parameters)
{
	using SimCoreProtocol::ETurnIndicator;
	bool Ok = true;
	for (double Start : {0.0, 0.43, 0.69, 17.395, 86400.719})
	{
		SimCoreTurnSignals::FPhaseClock Clock;
		for (int32 Cycle = 0; Cycle < 3; ++Cycle)
		{
			for (double Offset : {0.0, 0.43, 0.45, 0.71})
			{
				const double Relative = Cycle * SimCoreTurnSignals::PeriodSeconds + Offset;
				const double Phase = Clock.Update(ETurnIndicator::Left, Start + Relative, false);
				Ok &= TestTrue(TEXT("phase starts at the selection, independent of absolute world time"),
					FMath::IsNearlyEqual(Phase, Relative, 1.e-9));
				Ok &= TestEqual(TEXT("first and later flashes have the same complete on/off intervals"),
					USimCoreTurnSignals::IsLit(ETurnIndicator::Left, true, Phase, false), Offset < 0.44);
			}
		}
	}
	SimCoreTurnSignals::FPhaseClock Clock;
	Clock.Update(ETurnIndicator::Left, 20.3, false);
	Clock.Update(ETurnIndicator::Left, 20.8, false);
	Ok &= TestEqual(TEXT("switching direction begins a full new flash"), Clock.Update(ETurnIndicator::Right, 20.8, false), 0.0);
	Ok &= TestEqual(TEXT("entering hazards begins both lamps together"), Clock.Update(ETurnIndicator::Off, 20.9, true), 0.0);
	Ok &= TestEqual(TEXT("paused game clock preserves its exact phase"), Clock.Update(ETurnIndicator::Off, 20.9, true), 0.0);
	Clock.Update(ETurnIndicator::Off, 21.4, true);
	Ok &= TestTrue(TEXT("small snapshot-time correction cannot restart a flash"),
		FMath::IsNearlyEqual(Clock.Update(ETurnIndicator::Off, 21.39, true), 0.5, 1.e-9));
	Clock.Update(ETurnIndicator::Off, 21.4, false);
	Ok &= TestEqual(TEXT("cancel and reselect does not inherit the old off interval"), Clock.Update(ETurnIndicator::Left, 21.5, false), 0.0);
	Ok &= TestEqual(TEXT("new Play clock rebases without a negative or global phase"), Clock.Update(ETurnIndicator::Left, 0.0, false), 0.0);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePlayerIndicatorAutoCancelTest,
	"DriveIntegration.Presentation.TurnSignals.PlayerTurnThenCentreCancels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePlayerIndicatorAutoCancelTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool Ok = true;
	for (const ETurnIndicator Direction : {ETurnIndicator::Left, ETurnIndicator::Right})
	{
		SimCoreTurnSignals::FAutoCancel Cancel;
		SimCoreTurnSignals::FPhaseClock Clock;
		SimCoreVehicleAudio::FIndicatorClicks Clicks;
		FVehicleState State;
		State.PlaySessionId = TEXT("auto-cancel");
		State.LinearVelocityBody.X = 8.0;
		const float Side = Direction == ETurnIndicator::Left ? 1.0f : -1.0f;
		// Both directions cross compass north to exercise the 359/0 wrap.
		State.HeadingDegrees = Side > 0 ? 2.0f : 358.0f;
		const float StartHeading = State.HeadingDegrees;
		Ok &= TestFalse(TEXT("selection alone does not cancel"), Cancel.Update(Direction, false, State, true));
		Clock.Update(Direction, 17.395, false);
		Ok &= TestTrue(TEXT("first relay edge remains selection-relative"),
			Clicks.Update(Direction, Clock.GetElapsedSeconds(), false) == SimCoreVehicleAudio::EIndicatorClick::On);
		for (int32 Step = 1; Step <= 3; ++Step)
		{
			State.SimulationTimeNs = Step * 100000000ull;
			State.HeadingDegrees = FMath::Fmod(StartHeading - Side * Step * 3.0f + 360.0f, 360.0f);
			State.SteeringAngleRad = Side * 0.2f;
			Ok &= TestFalse(TEXT("actual 9-degree turn arms but does not extinguish a held rack"),
				Cancel.Update(Direction, false, State, true));
		}
		State.SimulationTimeNs = 400000000;
		State.SteeringAngleRad = Side * 0.05f;
		Ok &= TestFalse(TEXT("returning near centre is not yet centred"), Cancel.Update(Direction, false, State, true));
		State.SimulationTimeNs = 500000000;
		State.SteeringAngleRad = Side * 0.02f;
		Ok &= TestTrue(TEXT("a meaningful selected turn cancels only after rack centre return"),
			Cancel.Update(Direction, false, State, true));
		const double OffPhase = Clock.Update(ETurnIndicator::Off, 17.895, false);
		Ok &= TestFalse(TEXT("automatic cancellation extinguishes the left lamp on the same frame"),
			USimCoreTurnSignals::IsLit(ETurnIndicator::Off, true, OffPhase, false));
		Ok &= TestFalse(TEXT("automatic cancellation extinguishes the right lamp on the same frame"),
			USimCoreTurnSignals::IsLit(ETurnIndicator::Off, false, OffPhase, false));
		Ok &= TestTrue(TEXT("cancelled stalk cannot leave phantom relay clicks"),
			Clicks.Update(ETurnIndicator::Off, OffPhase, false) == SimCoreVehicleAudio::EIndicatorClick::None);
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePlayerIndicatorAutoCancelGuardTest,
	"DriveIntegration.Presentation.TurnSignals.PlayerAutoCancelMotionGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePlayerIndicatorAutoCancelGuardTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool Ok = true;
	// speed, rack, total selected heading change: none completes a genuine turn.
	for (const FVector3d Case : {FVector3d(0, .3, 20), FVector3d(-3, .3, 20),
		FVector3d(8, .06, 20), FVector3d(8, -.3, 20), FVector3d(8, .3, 4)})
	{
		SimCoreTurnSignals::FAutoCancel Cancel;
		FVehicleState State;
		State.PlaySessionId = TEXT("guard");
		State.LinearVelocityBody.X = Case.X;
		for (int32 Step = 0; Step <= 5; ++Step)
		{
			State.SimulationTimeNs = Step * 100000000ull;
			State.SteeringAngleRad = Step < 5 ? static_cast<float>(Case.Y) : 0.0f;
			State.HeadingDegrees = static_cast<float>(90 - Case.Z * FMath::Min(Step, 4) / 4.0);
			Ok &= TestFalse(TEXT("parked, reverse, light/opposite steering and tiny turns retain the stalk"),
				Cancel.Update(ETurnIndicator::Left, false, State, true));
		}
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePlayerIndicatorAutoCancelResetTest,
	"DriveIntegration.Presentation.TurnSignals.PlayerAutoCancelLifecycleGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCorePlayerIndicatorAutoCancelResetTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool Ok = true;
	for (int32 Case = 0; Case < 8; ++Case)
	{
		SimCoreTurnSignals::FAutoCancel Cancel;
		FVehicleState State;
		State.PlaySessionId = TEXT("old-play");
		State.LinearVelocityBody.X = 5;
		State.SteeringAngleRad = .2f;
		State.HeadingDegrees = 90;
		Cancel.Update(ETurnIndicator::Left, false, State, true);
		State.SimulationTimeNs = 100000000;
		State.HeadingDegrees = 80;
		Cancel.Update(ETurnIndicator::Left, false, State, true);
		State.SimulationTimeNs = 200000000;
		State.SteeringAngleRad = 0;
		ETurnIndicator Direction = ETurnIndicator::Left;
		bool bHazard = false, bFresh = true;
		switch (Case)
		{
		case 0: bHazard = true; break;
		case 1: Direction = ETurnIndicator::Right; break;
		case 2: bFresh = false; break;
		case 3: State.PlaySessionId = TEXT("new-play"); break;
		case 4: State.SimulationTimeNs = 50000000; break;
		case 5: State.SimulationTimeNs = 1000000000; break;
		case 6: Cancel.Reset(); break; // off/on of the same stalk between frames
		case 7: Direction = ETurnIndicator::Off; break;
		}
		Ok &= TestFalse(TEXT("hazard, reselection, stale state, new Play and clock discontinuity cannot reuse a prior turn"),
			Cancel.Update(Direction, bHazard, State, bFresh));
		State.SimulationTimeNs += 100000000;
		Ok &= TestFalse(TEXT("a fresh centred rack still needs a new selected turn after reset"),
			Cancel.Update(ETurnIndicator::Left, false, State, true));
	}
	SimCoreTurnSignals::FAutoCancel Cancel;
	FVehicleState State;
	State.PlaySessionId = TEXT("pause-stop"); State.LinearVelocityBody.X = 5;
	State.SteeringAngleRad = .2f; State.HeadingDegrees = 90;
	Cancel.Update(ETurnIndicator::Left, false, State, true);
	State.SimulationTimeNs = 100000000; State.HeadingDegrees = 80;
	Cancel.Update(ETurnIndicator::Left, false, State, true);
	State.SteeringAngleRad = 0;
	Ok &= TestFalse(TEXT("paused/repeated packet cannot complete a return to centre"),
		Cancel.Update(ETurnIndicator::Left, false, State, true));
	State.SimulationTimeNs = 200000000; State.LinearVelocityBody.X = 0;
	Ok &= TestFalse(TEXT("stopping mid-turn does not cancel while parked"),
		Cancel.Update(ETurnIndicator::Left, false, State, true));
	State.SimulationTimeNs = 300000000; State.LinearVelocityBody.X = 5;
	Ok &= TestTrue(TEXT("a previously completed turn may cancel once forward motion resumes centred"),
		Cancel.Update(ETurnIndicator::Left, false, State, true));
	return Ok;
}
#endif
