#include "SimCoreVehicleVisualApplication.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleVisualApplicationTest,
	"DriveIntegration.Presentation.VehicleVisualApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleVisualApplicationTest::RunTest(const FString& Parameters)
{
	using SimCoreProtocol::ERuntimeVehicleClass;
	using namespace SimCoreVehicleVisualApplication;
	TStrongObjectPtr<UStaticMesh> Sedan(NewObject<UStaticMesh>());
	TStrongObjectPtr<UStaticMesh> Compact(NewObject<UStaticMesh>());
	TStrongObjectPtr<UStaticMesh> Truck(NewObject<UStaticMesh>());
	TStrongObjectPtr<UStaticMesh> Motorcycle(NewObject<UStaticMesh>());
	const FBodyMeshes Meshes{Sedan.Get(), Compact.Get(), Truck.Get(), Motorcycle.Get()};
	struct FCase
	{
		ERuntimeVehicleClass Requested, Expected;
		UStaticMesh* Mesh;
	};
	const FCase Cases[] = {
		{ERuntimeVehicleClass::Unspecified, ERuntimeVehicleClass::Sedan, Sedan.Get()},
		{ERuntimeVehicleClass::Sedan, ERuntimeVehicleClass::Sedan, Sedan.Get()},
		{ERuntimeVehicleClass::Compact, ERuntimeVehicleClass::Compact, Compact.Get()},
		{ERuntimeVehicleClass::Truck, ERuntimeVehicleClass::Truck, Truck.Get()},
		{ERuntimeVehicleClass::Motorcycle, ERuntimeVehicleClass::Motorcycle, Motorcycle.Get()},
	};
	bool Ok = true;
	for (const FCase& Case : Cases)
	{
		const FBodySelection Selected = SelectBody(Case.Requested, Meshes);
		Ok &= TestEqual(TEXT("class normalization is shared"), Selected.VehicleClass, Case.Expected);
		Ok &= TestTrue(TEXT("selected asset identity is unchanged"), Selected.BodyMesh == Case.Mesh);
	}
	const auto UnknownClass = static_cast<ERuntimeVehicleClass>(255);
	const FBodySelection Unknown = SelectBody(UnknownClass, Meshes);
	Ok &= TestTrue(TEXT("unknown classes do not fall back to a visible body"),
		Unknown.VehicleClass == UnknownClass && Unknown.BodyMesh == nullptr);
	const FBodySelection Missing = SelectBody(ERuntimeVehicleClass::Truck,
		{Sedan.Get(), Compact.Get(), nullptr, Motorcycle.Get()});
	Ok &= TestTrue(TEXT("a missing authored body remains unavailable"),
		Missing.VehicleClass == ERuntimeVehicleClass::Truck && Missing.BodyMesh == nullptr);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("body replacement fixture"), Cube)) return false;
	TStrongObjectPtr<UStaticMeshComponent> Body(NewObject<UStaticMeshComponent>());
	Body->SetRelativeTransform(FTransform(FRotator(15.0, 30.0, -4.0),
		FVector(11.0, 22.0, 33.0), FVector(2.0)));
	Body->SetVisibility(false);
	Body->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
	Ok &= TestTrue(TEXT("fixture begins with a material override"), Body->GetNumOverrideMaterials() > 0);
	ReplaceBodyMesh(*Body, *Cube);
	Ok &= TestTrue(TEXT("replacement uses the requested body"), Body->GetStaticMesh() == Cube);
	Ok &= TestEqual(TEXT("replacement clears stale material overrides"), Body->GetNumOverrideMaterials(), 0);
	Ok &= TestTrue(TEXT("replacement keeps the authored local origin"),
		Body->GetRelativeTransform().Equals(FTransform::Identity));
	Ok &= TestTrue(TEXT("replacement makes the source body visible"), Body->IsVisible());
	return Ok;
}
#endif
