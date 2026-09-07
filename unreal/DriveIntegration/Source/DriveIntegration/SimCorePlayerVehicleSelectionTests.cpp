#if WITH_DEV_AUTOMATION_TESTS

#include "ExternalVehiclePawn.h"

#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "SimCoreClientComponent.h"
#include "SimCoreDriverPresentation.h"

namespace
{
UWorld* CreatePlayerVehicleTestWorld()
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
		.ShouldSimulatePhysics(false).SetTransactional(false).CreateFXSystem(false);
	return UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
			TEXT("PlayerVehicleSelectionQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePlayerVehiclePresentationTest,
	"DriveIntegration.Presentation.PlayerVehicleSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCorePlayerVehiclePresentationTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreatePlayerVehicleTestWorld();
	if (!TestNotNull(TEXT("transient vehicle-selection world"), World)) return false;
	AExternalVehiclePawn* Pawn = World->SpawnActor<AExternalVehiclePawn>();
	if (!TestNotNull(TEXT("player pawn"), Pawn))
	{
		World->DestroyWorld(false);
		return false;
	}

	USimCoreClientComponent* Client = Pawn->SimCoreClient;
	bool Ok = TestNotNull(TEXT("authoritative client"), Client);
	Ok &= TestFalse(TEXT("reselecting the active sedan is a no-op"),
		Client && Client->SelectVehicleClass(
			SimCoreProtocol::ERuntimeVehicleClass::Sedan));

	UInputComponent* Input = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(Input);
	const FName RequiredActions[] = {
		TEXT("SelectSedan"), TEXT("SelectCompact"),
		TEXT("SelectTruck"), TEXT("SelectMotorcycle")};
	for (const FName Action : RequiredActions)
	{
		bool bBound = false;
		for (int32 Index = 0; Index < Input->GetNumActionBindings(); ++Index)
		{
			if (Input->GetActionBinding(Index).GetActionName() == Action)
			{
				bBound = true;
				break;
			}
		}
		Ok &= TestTrue(*FString::Printf(TEXT("%s input is bound"), *Action.ToString()), bBound);
	}

	struct FCase
	{
		SimCoreProtocol::ERuntimeVehicleClass VehicleClass;
		const TCHAR* ExpectedAsset;
		int32 VisibleWheels;
	};
	const FCase Cases[] = {
		{SimCoreProtocol::ERuntimeVehicleClass::Sedan, TEXT("SM_SedanBody"), 4},
		{SimCoreProtocol::ERuntimeVehicleClass::Compact, TEXT("SM_CompactBody"), 4},
		{SimCoreProtocol::ERuntimeVehicleClass::Truck, TEXT("SM_TruckBody"), 4},
		{SimCoreProtocol::ERuntimeVehicleClass::Motorcycle, TEXT("SM_MotorcycleBody"), 2},
	};
	TSet<FString> MeshPaths;
	for (const FCase& Case : Cases)
	{
		Ok &= TestTrue(*FString::Printf(TEXT("configure %s"), Case.ExpectedAsset),
			Pawn->ConfigureVehicleClass(Case.VehicleClass));
		Ok &= TestEqual(TEXT("server-selected class drives presentation"),
			Pawn->GetDisplayedVehicleClass(), Case.VehicleClass);
		Ok &= TestEqual(TEXT("class-specific visible wheel count"),
			Pawn->GetVisibleWheelCount(), Case.VisibleWheels);
		const UStaticMesh* Mesh = Pawn->VehicleMesh->GetStaticMesh();
		Ok &= TestNotNull(TEXT("class body mesh"), Mesh);
		if (Mesh)
		{
			Ok &= TestTrue(TEXT("expected authored body asset"),
				Mesh->GetName().Contains(Case.ExpectedAsset));
			MeshPaths.Add(Mesh->GetPathName());
		}
	}
	Ok &= TestEqual(TEXT("all four player choices have distinct body assets"),
		MeshPaths.Num(), 4);
	Ok &= TestFalse(TEXT("motorcycle does not show sedan-only driver/interior"),
		Pawn->DriverPresentation->IsVisible());
	Ok &= TestTrue(TEXT("switching back restores sedan driver/interior"),
		Pawn->ConfigureVehicleClass(SimCoreProtocol::ERuntimeVehicleClass::Sedan)
			&& Pawn->DriverPresentation->IsVisible());

	World->DestroyWorld(false);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCorePlayerVehicleHelloValidationTest,
	"DriveIntegration.Network.PlayerVehicleSelectionHello",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCorePlayerVehicleHelloValidationTest::RunTest(const FString& Parameters)
{
	USimCoreClientComponent* Client = NewObject<USimCoreClientComponent>();
	Client->MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
	SimCoreProtocol::FHelloInfo Hello;
	Hello.Sequence = 1;
	Hello.SourceId = TEXT("simcore-host");
	Hello.MapPackageChecksum = Client->MapPackageChecksum;
	Hello.Build = TEXT("vehicle-selection-test");
	Hello.Schema = SimCoreProtocol::SchemaName;
	Hello.Capabilities = {
		TEXT("world-state.v2"),
		TEXT("control.v2"),
		TEXT("simulation-reset.v1"),
		TEXT("player-vehicle-selection.v1"),
		TEXT("map-package-checksum.v1"),
	};

	FString Error;
	bool Ok = TestTrue(TEXT("selection-capable server Hello is accepted"),
		Client->ValidateServerHello(Hello, Error));
	Hello.Capabilities.Remove(TEXT("player-vehicle-selection.v1"));
	Ok &= TestFalse(TEXT("server without player selection is rejected"),
		Client->ValidateServerHello(Hello, Error));
	Ok &= TestTrue(TEXT("rejection identifies the missing selection capability"),
		Error.Contains(TEXT("player-vehicle-selection.v1")));
	return Ok;
}

#endif
