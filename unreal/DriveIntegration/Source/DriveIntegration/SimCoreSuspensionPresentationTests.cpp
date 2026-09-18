#include "SimCoreSuspensionPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSuspensionLinksTest,
	"DriveIntegration.Presentation.SuspensionFrameAndWheelHubs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSuspensionLinksTest::RunTest(const FString& Parameters)
{
	FTransform Link;
	const FVector Start(100, -45, 15), End(113, -79, -27);
	bool Ok = TestTrue(TEXT("suspension link transform exists"),
		SimCoreSuspensionPresentation::LinkTransform(Start, End, 3.0f, Link));
	Ok &= TestTrue(TEXT("cylinder endpoints coincide exactly with body mount and wheel hub"),
		Link.TransformPosition(FVector(0, 0, -50)).Equals(Start, 0.001)
		&& Link.TransformPosition(FVector(0, 0, 50)).Equals(End, 0.001));
	Ok &= TestFalse(TEXT("zero length link is not rendered"),
		SimCoreSuspensionPresentation::LinkTransform(Start, Start, 3.0f, Link));
	const auto Values = UWorld::InitializationValues().CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("world"), World)) return false;
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Frame = NewObject<USimCoreSuspensionPresentation>(Owner);
	Owner->SetRootComponent(Frame);
	Owner->AddInstanceComponent(Frame);
	Frame->RegisterComponent();
	for (const auto Class : {SimCoreProtocol::ERuntimeVehicleClass::Sedan,
		SimCoreProtocol::ERuntimeVehicleClass::Compact, SimCoreProtocol::ERuntimeVehicleClass::Truck,
		SimCoreProtocol::ERuntimeVehicleClass::Motorcycle})
	{
		Frame->Configure(Class);
		const int32 Expected = Class == SimCoreProtocol::ERuntimeVehicleClass::Motorcycle ? 5
			: Class == SimCoreProtocol::ERuntimeVehicleClass::Truck ? 18 : 16;
		Ok &= TestEqual(TEXT("class-specific rails, arms, dampers and axle count"), Frame->GetVisibleLinkCount(), Expected);
		TArray<USceneComponent*> Children;
		Frame->GetChildrenComponents(false, Children);
		for (USceneComponent* Child : Children)
		{
			const auto* Mesh = Cast<UStaticMeshComponent>(Child);
			Ok &= TestTrue(TEXT("suspension meshes cannot add duplicate vehicle collision"),
				Mesh && Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		}
	}
	World->DestroyWorld(false);
	return Ok;
}
#endif
