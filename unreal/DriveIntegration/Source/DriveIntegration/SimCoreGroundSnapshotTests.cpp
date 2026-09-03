#include "SimCoreGroundSnapshot.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SimCoreMapPackage.h"
#include "SimCoreStaticCollider.h"
#include "SimCoreStaticCollisionSnapshot.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Never load, save, or mutate the user's editor world or MapPackage. */
	struct FGroundQaWorld
	{
		UWorld* World = nullptr;

		FGroundQaWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.SetTransactional(false)
				.CreateFXSystem(false);
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
					TEXT("SimCoreGroundQa")),
				GetTransientPackage(),
				true,
				ERHIFeatureLevel::Num,
				&Values);
		}

		~FGroundQaWorld()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
			}
		}
	};

	UBoxComponent* AddCollisionBox(
		UWorld* World,
		const FVector& Center,
		const FVector& HalfExtent,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None, RF_Transient);
		Actor->SetRootComponent(Box);
		Actor->AddInstanceComponent(Box);
		Box->InitBoxExtent(HalfExtent);
		Box->SetWorldLocationAndRotation(Center, Rotation);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetGenerateOverlapEvents(false);
		Box->SetCanEverAffectNavigation(false);
		Box->RegisterComponent();
		return Box;
	}

	SimCoreGroundSnapshot::FBuildRequest MakeRequest(UWorld* World)
	{
		SimCoreGroundSnapshot::FBuildRequest Request;
		Request.World = World;
		Request.HorizontalExtentXCm = 100.0;
		Request.HorizontalExtentYCm = 100.0;
		Request.SampleSpacingCm = 100.0;
		Request.TraceStartZCm = 1000.0;
		Request.TraceEndZCm = -1000.0;
		Request.MinimumGroundNormalZ = 0.1;
		Request.MaximumCellHeightDeltaCm = 100.0;
		return Request;
	}

	uint32 ReadUint32(const TArray<uint8>& Bytes, int32 Offset)
	{
		check(Offset >= 0 && Offset + 4 <= Bytes.Num());
		return static_cast<uint32>(Bytes[Offset])
			| static_cast<uint32>(Bytes[Offset + 1]) << 8
			| static_cast<uint32>(Bytes[Offset + 2]) << 16
			| static_cast<uint32>(Bytes[Offset + 3]) << 24;
	}

	float ReadFloat(const TArray<uint8>& Bytes, int32 Offset)
	{
		const uint32 Bits = ReadUint32(Bytes, Offset);
		float Value = 0.0f;
		FMemory::Memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	}

	bool TestCellMaterial(
		FAutomationTestBase& Test,
		const SimCoreGroundSnapshot::FBuildResult& Snapshot,
		uint32 ExpectedMaterial,
		float ExpectedFriction)
	{
		bool bSuccess = Test.TestEqual(TEXT("SIMGHF2 byte count"), Snapshot.Binary.Num(), 308);
		if (!bSuccess)
		{
			return false;
		}
		const TArray<uint8>& Bytes = Snapshot.Binary;
		bSuccess &= Test.TestTrue(TEXT("SIMGHF2 magic"),
			FMemory::Memcmp(Bytes.GetData(), "SIMGHF2\0", 8) == 0);
		bSuccess &= Test.TestEqual(TEXT("snapshot version"), ReadUint32(Bytes, 8), 2u);
		bSuccess &= Test.TestEqual(TEXT("sample stride"), ReadUint32(Bytes, 24), 20u);
		bSuccess &= Test.TestEqual(TEXT("material cell stride"), ReadUint32(Bytes, 28), 12u);
		for (int32 Cell = 0; Cell < 4; ++Cell)
		{
			const int32 Offset = 80 + 9 * 20 + Cell * 12;
			bSuccess &= Test.TestEqual(TEXT("measured cell is drivable"), ReadUint32(Bytes, Offset), 1u);
			bSuccess &= Test.TestEqual(TEXT("serialized material id"), ReadUint32(Bytes, Offset + 4), ExpectedMaterial);
			bSuccess &= Test.TestTrue(TEXT("serialized friction multiplier"),
				FMath::IsNearlyEqual(ReadFloat(Bytes, Offset + 8), ExpectedFriction, 1.0e-6f));
		}
		return bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreGroundWorldMaterialsTest,
	"DriveIntegration.Ground.WorldCollisionMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreGroundWorldMaterialsTest::RunTest(const FString& Parameters)
{
	// A new Chaos solver copies the registered query-material table when its
	// world is created. Register the fixture material first: unlike an editor
	// world, this isolated test world does not tick to synchronize later ones.
	TStrongObjectPtr<UPhysicalMaterial> RoughMaterial(
		NewObject<UPhysicalMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
	RoughMaterial->SurfaceType = SurfaceType3;
	RoughMaterial->GetPhysicsMaterial();
	FGroundQaWorld Scene;
	if (!TestNotNull(TEXT("isolated collision world"), Scene.World))
	{
		return false;
	}
	UBoxComponent* Ground = AddCollisionBox(Scene.World, FVector(0.0, 0.0, -25.0), FVector(250.0, 250.0, 25.0));
	if (!TestNotNull(TEXT("WorldStatic ground"), Ground))
	{
		return false;
	}
	auto Request = MakeRequest(Scene.World);
	SimCoreGroundSnapshot::FBuildResult Snapshot;
	FString Error;
	Ground->GetOwner()->Tags.Add(TEXT("SimCore.Surface.Asphalt"));
	bool bSuccess = TestTrue(TEXT("sample actual WorldStatic asphalt collision"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	if (!bSuccess)
	{
		AddError(Error);
		return false;
	}
	bSuccess &= TestEqual(TEXT("all nine traces hit"), Snapshot.ValidSampleCount, 9);
	bSuccess &= TestTrue(TEXT("spawn cell is covered"), Snapshot.bMapOriginCovered);
	bSuccess &= TestEqual(TEXT("asphalt cells"), Snapshot.AsphaltMaterialCellCount, 4);
	bSuccess &= TestCellMaterial(*this, Snapshot, 1u, 1.0f);

	Ground->ComponentTags.Add(TEXT("SimCore.Surface.LowFriction"));
	bSuccess &= TestTrue(TEXT("component material tag overrides actor tag"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	bSuccess &= TestEqual(TEXT("low-friction cells"), Snapshot.LowFrictionMaterialCellCount, 4);
	bSuccess &= TestCellMaterial(*this, Snapshot, 2u, 0.35f);

	Ground->ComponentTags.Reset();
	Ground->GetOwner()->Tags.Reset();
	Ground->SetPhysMaterialOverride(RoughMaterial.Get());
	FCollisionObjectQueryParams MaterialObjects;
	MaterialObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams MaterialQuery(SCENE_QUERY_STAT(SimCoreGroundQaMaterial), true);
	MaterialQuery.bReturnPhysicalMaterial = true;
	FHitResult MaterialHit;
	bSuccess &= TestTrue(TEXT("physical-surface fixture collision hit"), Scene.World->LineTraceSingleByObjectType(
		MaterialHit, FVector(0.0, 0.0, 1000.0), FVector(0.0, 0.0, -1000.0), MaterialObjects, MaterialQuery));
	bSuccess &= TestTrue(TEXT("physical-surface fixture is synchronized into the collision scene"),
		MaterialHit.PhysMaterial.Get() == RoughMaterial.Get());
	bSuccess &= TestTrue(TEXT("Physical Surface fallback measured from collision hit"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	bSuccess &= TestEqual(TEXT("rough physical-surface cells"), Snapshot.RoughMaterialCellCount, 4);
	bSuccess &= TestCellMaterial(*this, Snapshot, 3u, 1.10f);

	Ground->ComponentTags = {FName(TEXT("SimCore.Surface.Asphalt")), FName(TEXT("SimCore.Surface.Rough"))};
	bSuccess &= TestFalse(TEXT("ambiguous component material fails closed"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	bSuccess &= TestTrue(TEXT("conflicting material diagnostic"), Error.Contains(TEXT("conflicting")));
	bSuccess &= TestEqual(TEXT("rejected bake does not expose a payload"), Snapshot.Binary.Num(), 0);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreGroundSlopeCoverageTest,
	"DriveIntegration.Ground.SlopeAndCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreGroundSlopeCoverageTest::RunTest(const FString& Parameters)
{
	FGroundQaWorld Scene;
	if (!TestNotNull(TEXT("isolated slope world"), Scene.World))
	{
		return false;
	}
	const FRotator SlopeRotation(20.0, 0.0, 0.0);
	UBoxComponent* Ground = AddCollisionBox(Scene.World, FVector(0.0, 0.0, -25.0),
		FVector(500.0, 500.0, 25.0), SlopeRotation);
	UBoxComponent* Overhead = AddCollisionBox(Scene.World, FVector(0.0, 0.0, 500.0), FVector(250.0, 250.0, 25.0));
	if (!TestNotNull(TEXT("sloped collision"), Ground) || !TestNotNull(TEXT("unrelated overhead collider"), Overhead))
	{
		return false;
	}
	auto Request = MakeRequest(Scene.World);
	Request.GroundActor = Ground->GetOwner();
	SimCoreGroundSnapshot::FBuildResult Snapshot;
	FString Error;
	bool bSuccess = TestTrue(TEXT("selected ground remains visible below unrelated overhead collision"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	if (!bSuccess)
	{
		AddError(Error);
		return false;
	}
	bSuccess &= TestEqual(TEXT("all slope samples measured"), Snapshot.ValidSampleCount, 9);
	bSuccess &= TestTrue(TEXT("slope height follows upward-forward pitch"),
		ReadFloat(Snapshot.Binary, 80 + 2 * 20 + 4) > ReadFloat(Snapshot.Binary, 80 + 4));
	const FVector Normal = SlopeRotation.RotateVector(FVector::UpVector);
	bSuccess &= TestTrue(TEXT("measured normal uses ENU east=UE_Y, north=UE_X"),
		FMath::IsNearlyEqual(ReadFloat(Snapshot.Binary, 80 + 8), static_cast<float>(Normal.Y), 1.0e-5f)
		&& FMath::IsNearlyEqual(ReadFloat(Snapshot.Binary, 80 + 12), static_cast<float>(Normal.X), 1.0e-5f)
		&& FMath::IsNearlyEqual(ReadFloat(Snapshot.Binary, 80 + 16), static_cast<float>(Normal.Z), 1.0e-5f));
	bSuccess &= TestTrue(TEXT("overhead was not exported as the ground"), Snapshot.MaximumEnuM.Z < 1.0);

	Request.GroundActor = nullptr;
	bSuccess &= TestTrue(TEXT("unfiltered trace chooses upper WorldStatic surface"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	bSuccess &= TestTrue(TEXT("unfiltered overhead height"), FMath::IsNearlyEqual(Snapshot.MinimumEnuM.Z, 5.25, 1.0e-5));

	Request.GroundActor = Ground->GetOwner();
	Request.MaximumCellHeightDeltaCm = 1.0;
	Request.bApplyHeightDiscontinuityFilter = true;
	bSuccess &= TestFalse(TEXT("unfiltered-style discontinuity guard rejects steep cells"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	Request.bApplyHeightDiscontinuityFilter = false;
	bSuccess &= TestTrue(TEXT("explicit authored slope is not cut into holes by a height threshold"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));

	Request.MaximumSampleCount = 4;
	bSuccess &= TestFalse(TEXT("sample budget fails without silently reducing resolution"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	bSuccess &= TestTrue(TEXT("sample budget diagnostic"), Error.Contains(TEXT("requires 9 samples")));
	Request.MaximumSampleCount = SimCoreGroundSnapshot::HardMaximumSampleCount;
	Request.MapOriginWorldCm = FVector(1000.0, 0.0, 0.0);
	bSuccess &= TestFalse(TEXT("uncovered spawn is rejected"),
		SimCoreGroundSnapshot::Build(Request, Snapshot, Error));
	bSuccess &= TestTrue(TEXT("uncovered spawn diagnostic"), Error.Contains(TEXT("spawn")));
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreStaticMarkerPackageTest,
	"DriveIntegration.Ground.StaticMarkerPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreStaticMarkerPackageTest::RunTest(const FString& Parameters)
{
	FGroundQaWorld Scene;
	if (!TestNotNull(TEXT("isolated marker world"), Scene.World))
	{
		return false;
	}
	ASimCoreStaticCollider* Wall = Scene.World->SpawnActor<ASimCoreStaticCollider>();
	ASimCoreStaticCollider* Curb = Scene.World->SpawnActor<ASimCoreStaticCollider>();
	ASimCoreStaticCollider* Disabled = Scene.World->SpawnActor<ASimCoreStaticCollider>();
	if (!TestNotNull(TEXT("wall marker"), Wall) || !TestNotNull(TEXT("curb marker"), Curb)
		|| !TestNotNull(TEXT("disabled marker"), Disabled))
	{
		return false;
	}
	Wall->ColliderId = TEXT("z_wall");
	Wall->Semantic = ESimCoreStaticColliderSemantic::Wall;
	Wall->SetActorLocationAndRotation(FVector(100.0, 200.0, 300.0), FRotator(0.0, 90.0, 0.0));
	Wall->CollisionBounds->SetBoxExtent(FVector(200.0, 25.0, 100.0));
	Wall->SetActorScale3D(FVector(2.0, 3.0, 1.0));
	Curb->ColliderId = TEXT("a_curb");
	Curb->Semantic = ESimCoreStaticColliderSemantic::Curb;
	Disabled->bExportEnabled = false;
	Disabled->ColliderId.Reset(); // Disabled markers must not fail authoring validation.
	FString Csv;
	FString Error;
	int32 Count = 0;
	bool bSuccess = TestTrue(TEXT("serialize real registered marker actors"),
		SimCoreStaticCollisionSnapshot::BuildCsv(Scene.World, FVector::ZeroVector, Csv, Count, Error));
	bSuccess &= TestEqual(TEXT("disabled marker omitted"), Count, 2);
	bSuccess &= TestTrue(TEXT("stable ID order"), Csv.Find(TEXT("a_curb,")) < Csv.Find(TEXT("z_wall,")));
	bSuccess &= TestTrue(TEXT("OBB coordinate, yaw and component-scale contract"), Csv.Contains(
		TEXT("z_wall,wall,obb,2.000000,1.000000,3.000000,1.570796327,4.000000,0.750000,1.000000,0.800000,0.000000")));

	Curb->ColliderId = Wall->ColliderId;
	bSuccess &= TestFalse(TEXT("duplicate IDs rejected before payload commit"),
		SimCoreStaticCollisionSnapshot::BuildCsv(Scene.World, FVector::ZeroVector, Csv, Count, Error));
	bSuccess &= TestTrue(TEXT("duplicate diagnostic"), Error.Contains(TEXT("Duplicate")));
	bSuccess &= TestTrue(TEXT("invalid marker emits no partial CSV"), Csv.IsEmpty());
	Curb->ColliderId = TEXT("a_curb");
	Wall->SetActorRotation(FRotator(5.0, 90.0, 0.0));
	bSuccess &= TestFalse(TEXT("pitched marker rejected: prisms are yaw-only"),
		SimCoreStaticCollisionSnapshot::BuildCsv(Scene.World, FVector::ZeroVector, Csv, Count, Error));
	bSuccess &= TestTrue(TEXT("upright diagnostic"), Error.Contains(TEXT("upright")));

	// Keep the retained fixture useful to the actual server: a 40 m footprint
	// with a clear spawn, plus two explicit obstacles well away from the car.
	Wall->SetActorLocationAndRotation(FVector(1500.0, 1500.0, 100.0), FRotator(0.0, 90.0, 0.0));
	Curb->SetActorLocation(FVector(1500.0, -1500.0, 15.0));
	Curb->CollisionBounds->SetBoxExtent(FVector(200.0, 25.0, 15.0));
	bSuccess &= TestTrue(TEXT("serialize spawn-safe fixture markers"),
		SimCoreStaticCollisionSnapshot::BuildCsv(Scene.World, FVector::ZeroVector, Csv, Count, Error));
	const FString ValidCsv = Csv;
	UBoxComponent* Ground = AddCollisionBox(Scene.World, FVector(0.0, 0.0, -25.0), FVector(2500.0, 2500.0, 25.0));
	if (!TestNotNull(TEXT("package ground collision"), Ground))
	{
		return false;
	}
	Ground->ComponentTags.Add(TEXT("SimCore.Surface.LowFriction"));
	SimCoreGroundSnapshot::FBuildResult Snapshot;
	auto PackageRequest = MakeRequest(Scene.World);
	PackageRequest.HorizontalExtentXCm = 2000.0;
	PackageRequest.HorizontalExtentYCm = 2000.0;
	if (!TestTrue(TEXT("real collision snapshot for package fixture"),
		SimCoreGroundSnapshot::Build(PackageRequest, Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation/SimCoreGroundQa"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	if (!TestTrue(TEXT("create isolated Saved automation artifact directory"), IFileManager::Get().MakeDirectory(*Directory, true)))
	{
		return false;
	}
	const FString Sentinel = TEXT("# Automation fixture measured from transient Unreal WorldStatic collision.\n")
		TEXT("# terrain_payload=ground_heightfield.bin format=SIMGHF2\n")
		TEXT("surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2\n");
	bSuccess &= TestTrue(TEXT("write measured SIMGHF2 fixture"), FFileHelper::SaveArrayToFile(
		Snapshot.Binary, *FPaths::Combine(Directory, TEXT("ground_heightfield.bin"))));
	bSuccess &= TestTrue(TEXT("write static marker fixture"), FFileHelper::SaveStringToFile(
		ValidCsv, *FPaths::Combine(Directory, TEXT("static_colliders.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	bSuccess &= TestTrue(TEXT("write compatibility sentinel"), FFileHelper::SaveStringToFile(
		Sentinel, *FPaths::Combine(Directory, TEXT("ground_surface.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const TArray<FString> CollisionFiles{
		TEXT("ground_surface.csv"), TEXT("ground_heightfield.bin"), TEXT("static_colliders.csv")};
	FString Checksum;
	bSuccess &= TestTrue(TEXT("manifest is committed after collision payloads"), SimCoreMapPackage::WriteManifestLast(
		Directory, TEXT("automation_ground_qa"), CollisionFiles, Checksum, Error));
	SimCoreMapPackage::FManifest Manifest;
	bSuccess &= TestTrue(TEXT("Unreal verifies the emitted package identity"),
		SimCoreMapPackage::LoadAndVerifyManifest(Directory, Manifest, Error));
	bSuccess &= TestEqual(TEXT("verified identity matches committed identity"), Manifest.CollisionChecksum, Checksum);
	AddInfo(FString::Printf(TEXT("Measured QA package retained for cross-runtime loader verification: %s"), *Directory));
	return bSuccess;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
