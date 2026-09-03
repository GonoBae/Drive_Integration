#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "ExternalVehiclePawn.h"
#include "Misc/AutomationTest.h"
#include "Particles/ParticleSystem.h"
#include "SimCoreExhaustComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreExhaustPresentationTest,
	"DriveIntegration.VehicleEffects.ExhaustPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreExhaustPresentationTest::RunTest(const FString& Parameters)
{
	using SimCoreExhaustPresentation::BuildSample;

	const auto StoppedEngine = BuildSample(0.0f, 0.0f, 0.0f, 0.1f, 2.0f, 14.0f);
	TestFalse(TEXT("stopped engine emits no exhaust"), StoppedEngine.bEngineRunning);
	TestEqual(TEXT("stopped engine spawn rate is zero"), StoppedEngine.SpawnRatePerSecond, 0.0f);

	const auto Idle = BuildSample(750.0f, 0.0f, 0.01f, 0.1f, 2.0f, 14.0f);
	TestTrue(TEXT("idle engine has a faint plume"), Idle.bEngineRunning);
	TestTrue(TEXT("idle rate is bounded"), Idle.SpawnRatePerSecond >= 2.0f && Idle.SpawnRatePerSecond < 6.0f);

	const auto Loaded = BuildSample(4800.0f, 3.5f, 0.01f, 0.1f, 2.0f, 14.0f);
	TestTrue(TEXT("load increases exhaust density"), Loaded.SpawnRatePerSecond > Idle.SpawnRatePerSecond);
	TestTrue(TEXT("load remains capped"), Loaded.SpawnRatePerSecond <= 14.0f);

	const auto Stale = BuildSample(4800.0f, 3.5f, 0.2f, 0.1f, 2.0f, 14.0f);
	TestFalse(TEXT("stale authority disables new particles"), Stale.bEngineRunning);
	TestEqual(TEXT("stale spawn rate is zero"), Stale.SpawnRatePerSecond, 0.0f);

	const auto Invalid = BuildSample(NAN, 0.0f, 0.0f, 0.1f, 2.0f, 14.0f);
	TestFalse(TEXT("invalid telemetry fails closed"), Invalid.bEngineRunning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreExhaustRuntimeComponentTest,
	"DriveIntegration.VehicleEffects.ExhaustRuntimeComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreExhaustRuntimeComponentTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreExhaustQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated exhaust QA world"), World))
	{
		return false;
	}

	AExternalVehiclePawn* Pawn = World->SpawnActor<AExternalVehiclePawn>();
	USimCoreExhaustComponent* Exhaust = Pawn
		? Pawn->FindComponentByClass<USimCoreExhaustComponent>()
		: nullptr;
	bool bSuccess = TestNotNull(TEXT("vehicle owns an exhaust presentation component"), Exhaust);
	if (Exhaust)
	{
		UParticleSystem* RuntimeTemplate = Exhaust->Template;
		bSuccess &= TestNotNull(TEXT("component builds its asset-free runtime template"), RuntimeTemplate);
		bSuccess &= TestTrue(TEXT("runtime template owns one bounded emitter"),
			RuntimeTemplate && RuntimeTemplate->Emitters.Num() == 1);
		bSuccess &= TestTrue(TEXT("tailpipe placement stays behind the rear axle"),
			Exhaust->GetRelativeLocation().Equals(FVector(-221.0, 55.0, -25.0), 0.01));
	}
	World->DestroyWorld(false);
	return bSuccess;
}

#endif
