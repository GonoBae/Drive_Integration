#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreTurnSignals.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreSedanVisualContract.h"
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
	State.SimulationTimeNs = 100000000;
	bool Ok = TestTrue(TEXT("real NPC snapshot applies"), Actor->ApplySnapshot(State,0,0,true,.15f,FVector::ZeroVector));
	auto* SourceMaterial = FindSignalMaterial(Actor->GetBody());
	Ok &= TestTrue(TEXT("authored source lens blinks left independently"), HasSignalValues(SourceMaterial,1,0));
	TArray<UStaticMeshComponent*> Meshes;
	Actor->GetComponents(Meshes);
	Ok &= TestEqual(TEXT("body, wheels and the authored cabin interior exist"),
		Meshes.Num(), 17);
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
	Signals->UpdateSignal(ETurnIndicator::Left,.5,false);
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
#endif
