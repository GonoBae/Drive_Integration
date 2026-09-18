#include "SimCoreVehicleVisualProfile.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SimCoreSedanVisualContract.h"
#include "Engine/StaticMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleVisualProfileTest,
	"DriveIntegration.Presentation.VehicleVisualProfiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleVisualProfileTest::RunTest(const FString& Parameters)
{
	using SimCoreProtocol::ERuntimeVehicleClass;
	using namespace SimCoreVehicleVisualProfile;
	struct FCase
	{
		ERuntimeVehicleClass Class;
		FVector Front, Rear, Scale, Exhaust;
		float HalfHeight;
	};
	const FCase Cases[] = {
		{ERuntimeVehicleClass::Sedan, {121.5, -79.0, -23.0}, {-148.5, -79.0, -23.0},
			FVector::OneVector, {-221.0, 55.0, -25.0}, 0.75f},
		{ERuntimeVehicleClass::Compact, {95.985, -71.1, -22.66}, {-117.315, -71.1, -22.66},
			{0.86, 0.82, 0.86}, {-170.0, 48.0, -22.0}, 0.70f},
		{ERuntimeVehicleClass::Truck, {190.0, -102.0, -28.0}, {-195.0, -102.0, -28.0},
			{1.22, 1.08, 1.22}, {-290.0, 86.0, -15.0}, 0.965f},
		{ERuntimeVehicleClass::Motorcycle, {99.0, 0.0, -8.0}, {-82.0, 0.0, -8.0},
			{1.05, 0.42, 1.05}, {-102.0, 18.0, 18.0}, 0.68f},
	};
	bool Ok = true;
	FProfile Profile;
	for (const FCase& Case : Cases)
	{
		Ok &= TestTrue(TEXT("supported vehicle profile"), Resolve(Case.Class, Profile));
		Ok &= TestEqual(TEXT("profile class"), Profile.VehicleClass, Case.Class);
		Ok &= TestTrue(TEXT("front axle unchanged"), Profile.WheelOriginsCm[0].Equals(Case.Front, 1.e-6));
		Ok &= TestTrue(TEXT("rear axle unchanged"), Profile.WheelOriginsCm[2].Equals(Case.Rear, 1.e-6));
		Ok &= TestTrue(TEXT("wheel scale unchanged"), Profile.WheelScales[0].Equals(Case.Scale, 1.e-6));
		Ok &= TestTrue(TEXT("exhaust unchanged"), Profile.ExhaustLocationCm.Equals(Case.Exhaust, 1.e-6));
		Ok &= TestEqual(TEXT("presentation half height unchanged"), Profile.HalfHeightMeters, Case.HalfHeight);
		int32 WheelCount = 0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			WheelCount += Profile.IsWheelVisible(Index) ? 1 : 0;
			Ok &= TestFalse(TEXT("lamp positions finite"), Profile.LampLocationsCm[Index].ContainsNaN());
		}
		Ok &= TestEqual(TEXT("visible wheel count"), WheelCount, Case.Class == ERuntimeVehicleClass::Motorcycle ? 2 : 4);
		Ok &= TestFalse(TEXT("negative wheel index rejected"), Profile.IsWheelVisible(-1));
		Ok &= TestFalse(TEXT("out of range wheel index rejected"), Profile.IsWheelVisible(4));
		if (Case.Class == ERuntimeVehicleClass::Motorcycle)
		{
			Ok &= TestTrue(TEXT("unused motorcycle anchors remain zero"),
				Profile.WheelOriginsCm[1].IsZero() && Profile.WheelOriginsCm[3].IsZero());
		}
		else
		{
			Ok &= TestTrue(TEXT("left and right front wheels remain symmetric"),
				Profile.WheelOriginsCm[1].Equals(FVector(Case.Front.X, -Case.Front.Y, Case.Front.Z), 1.e-6));
		}
	}
	Ok &= TestTrue(TEXT("legacy unspecified class resolves"), Resolve(ERuntimeVehicleClass::Unspecified, Profile));
	Ok &= TestEqual(TEXT("legacy class remains sedan"), Profile.VehicleClass, ERuntimeVehicleClass::Sedan);
	Ok &= TestFalse(TEXT("unknown class rejected"), Resolve(static_cast<ERuntimeVehicleClass>(255), Profile));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTruckCollisionBoundsTest,
	"DriveIntegration.Presentation.TruckCollisionBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreTruckCollisionBoundsTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	FProfile Profile;
	if (!TestTrue(TEXT("Truck profile resolves"),
		Resolve(SimCoreProtocol::ERuntimeVehicleClass::Truck, Profile))) return false;
	const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Vehicles/NpcFleet/SM_TruckBody.SM_TruckBody"));
	if (!TestNotNull(TEXT("Authored truck body"), Mesh)) return false;
	const FBox Body = Mesh->GetBoundingBox();
	const FVector Centre = CollisionCenterOffsetCm(Profile, Profile.HalfHeightMeters);
	const FVector Extent(310.0, 105.0, Profile.HalfHeightMeters * 100.0);
	bool Ok = TestTrue(TEXT("Collision centre matches the truck mesh, not the CG"),
		Centre.Equals(Body.GetCenter(), 0.001));
	Ok &= TestTrue(TEXT("Collision half extents match the actual body asset"),
		Extent.Equals(Body.GetExtent(), 0.001));
	Ok &= TestTrue(TEXT("Front, rear, sides, roof and chassis bottom fit the body"),
		(Centre - Extent).Equals(Body.Min, 0.001)
		&& (Centre + Extent).Equals(Body.Max, 0.001));
	for (const double Yaw : {0.0, 90.0, 180.0, 270.0})
	{
		const FTransform Pose(FRotator(0.0, Yaw, 0.0), FVector(700.0, -500.0, 75.0));
		const FVector WorldCentre = Pose.TransformPosition(Centre);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Offset((Corner & 1) ? Extent.X : -Extent.X,
				(Corner & 2) ? Extent.Y : -Extent.Y, (Corner & 4) ? Extent.Z : -Extent.Z);
			const FVector BodyCorner((Corner & 1) ? Body.Max.X : Body.Min.X,
				(Corner & 2) ? Body.Max.Y : Body.Min.Y, (Corner & 4) ? Body.Max.Z : Body.Min.Z);
			Ok &= TestTrue(TEXT("Centre offset rotates with the truck heading"),
				(WorldCentre + Pose.TransformVectorNoScale(Offset)).Equals(
					Pose.TransformPosition(BodyCorner), 0.001));
		}
	}
	return Ok;
}
#endif
