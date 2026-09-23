#include "SimCoreVehicleVisualProfile.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SimCorePresentation.h"
#include "SimCoreSedanVisualContract.h"

namespace
{
struct FFixture
{
	FString Root;
	FString Parent;
	bool bReady = false;
	FFixture()
	{
		Parent = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/VehicleCatalog")));
		Root = FPaths::Combine(Parent, FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString Source = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("VehicleCatalog"));
		bReady = FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*Root, *Source, true);
	}
	~FFixture()
	{
		if (FPaths::GetPath(Root) == Parent && FPaths::GetCleanFilename(Root).Len() == 32)
			IFileManager::Get().DeleteDirectory(*Root, false, true);
	}
	bool Change(const TCHAR* File, const FString& Before, const FString& After) const
	{
		FString Text;
		const FString Path = FPaths::Combine(Root, File);
		if (!FFileHelper::LoadFileToString(Text, *Path) || !Text.Contains(Before)) return false;
		Text.ReplaceInline(*Before, *After, ESearchCase::CaseSensitive);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	bool Load(SimCoreVehicleVisualProfile::FCatalog& Out, FString& Error) const
	{
		return bReady && SimCoreVehicleVisualProfile::LoadCatalog(FPaths::Combine(Root, TEXT("catalog.json")), Out, Error);
	}
	bool Edit(const TCHAR* File, TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Mutate) const
	{
		const FString Path = FPaths::Combine(Root, File);
		FString Text;
		TSharedPtr<FJsonObject> Object;
		if (!FFileHelper::LoadFileToString(Text, *Path)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object)) return false;
		Mutate(Object);
		Text.Reset();
		if (!FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text))) return false;
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
};

TSharedPtr<FJsonObject> PowertrainSelection()
{
	const auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("engine"), TEXT("engine_sedan_gasoline_2_0"));
	Result->SetStringField(TEXT("transmission"), TEXT("transmission_sedan_automatic_6speed"));
	Result->SetStringField(TEXT("drivetrain"), TEXT("drivetrain_sedan_rwd"));
	Result->SetStringField(TEXT("fuel_tank"), TEXT("fuel_tank_sedan_50l"));
	Result->SetNumberField(TEXT("initial_fuel_l"), 40);
	Result->SetNumberField(TEXT("upshift_rpm"), 5200);
	Result->SetNumberField(TEXT("downshift_rpm"), 1800);
	Result->SetNumberField(TEXT("shift_duration_s"), .25);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleCatalogParityTest,
	"DriveIntegration.Presentation.VehicleCatalogParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleCatalogParityTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	FFixture Fixture;
	FCatalog Catalog;
	FString Error, LiveChecksum;
	if (!TestTrue(TEXT("isolated shared catalog loaded"), Fixture.Load(Catalog, Error)))
	{
		AddError(Error);
		return false;
	}
	bool Ok = TestTrue(TEXT("live catalog identity available"), CatalogChecksum(LiveChecksum, Error));
	Ok &= TestEqual(TEXT("checksum independent of absolute directory"), Catalog.Checksum, LiveChecksum);
	Ok &= TestEqual(TEXT("raw byte identity agrees with server parity fixture"),
		Catalog.Checksum, FString(TEXT("fnv1a64:c7ca79543fe70535")));
	const FVector DriverOrigins[] = {{0,0,0}, {-4,0,-1.5}, {110,0,62}, {0,0,0}};
	const FVector DriverScales[] = {{1,1,1}, {.79,.90,.92}, {.8,1,1}, {1,1,1}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FProfile Profile;
		Ok &= TestTrue(TEXT("frozen class resolves"), ResolveFromCatalog(Catalog,
			static_cast<SimCoreProtocol::ERuntimeVehicleClass>(Index + 1), Profile));
		Ok &= TestTrue(TEXT("driver mounting translation unchanged"), Profile.DriverTransform.GetLocation().Equals(DriverOrigins[Index], 1.e-9));
		Ok &= TestTrue(TEXT("driver mounting scale unchanged"), Profile.DriverTransform.GetScale3D().Equals(DriverScales[Index], 1.e-9));
		Ok &= TestFalse(TEXT("default front tire module remains inherited"), Profile.PlayerAxleTireRadiusMeters[0].IsSet());
		Ok &= TestFalse(TEXT("default rear tire module remains inherited"), Profile.PlayerAxleTireRadiusMeters[1].IsSet());
		if (Index < 2)
		{
			for (int32 Lamp = 0; Lamp < 4; ++Lamp)
			{
				FVector Expected = SimCoreSedanVisualContract::TurnSignalLensPointCm(Lamp < 2, Lamp % 2 == 0, .5, .5);
				if (Index == 1) Expected = FVector(Expected.X * .79 - 4, Expected.Y * .90, Expected.Z * .92 - 1.5);
				Ok &= TestTrue(TEXT("lamp remains on authored curved lens"), Profile.LampLocationsCm[Lamp].Equals(Expected, 1.e-8));
			}
		}
	}
	Ok &= TestEqual(TEXT("truck player ground clearance preserved"), Catalog.Profiles[2].CollisionGroundClearanceMeters, .49f);
	Ok &= TestEqual(TEXT("truck NPC ground clearance preserved separately"), Catalog.Profiles[2].NpcCollisionGroundClearanceMeters, .411071f);
	Ok &= TestTrue(TEXT("one-file mutation written"), Fixture.Change(TEXT("profiles/truck.json"), TEXT("0.411071"), TEXT("0.411072")));
	FCatalog Changed;
	Ok &= TestTrue(TEXT("valid visual edit loads"), Fixture.Load(Changed, Error));
	Ok &= TestNotEqual(TEXT("any shared file byte change changes identity"), Changed.Checksum, Catalog.Checksum);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleCatalogRejectionTest,
	"DriveIntegration.Presentation.VehicleCatalogStrictValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleCatalogRejectionTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	struct FMutation { const TCHAR* File; const TCHAR* Before; const TCHAR* After; };
	const FMutation Mutations[] = {
		{TEXT("catalog.json"), TEXT("profiles/truck.json"), TEXT("../truck.json")},
		{TEXT("catalog.json"), TEXT("profiles/truck.json"), TEXT("profiles/sedan.json")},
		{TEXT("catalog.json"), TEXT("\"schema_version\": 1"), TEXT("\"schema_version\": 2")},
		{TEXT("profiles/truck.json"), TEXT("\"vehicle_class\": 3"), TEXT("\"vehicle_class\": 2")},
		{TEXT("profiles/truck.json"), TEXT("truck_standard"), TEXT("sedan_standard")},
		{TEXT("profiles/truck.json"), TEXT("\"half_height_m\": 0.965"), TEXT("\"half_height_m\": \"0.965\"")},
		{TEXT("profiles/truck.json"), TEXT("\"half_height_m\": 0.965"), TEXT("\"half_height_m\": -1")},
		{TEXT("profiles/truck.json"), TEXT("\"half_height_m\": 0.965"), TEXT("\"half_heigth_m\": 0.965")},
		{TEXT("profiles/truck.json"), TEXT("\"half_height_m\": 0.965"), TEXT("\"half_height_m\": 0.965, \"half_\\u0068eight_m\": 1")},
		{TEXT("profiles/truck.json"), TEXT("\"half_height_m\": 0.965"), TEXT("\"half_height_m\": 1e999")},
	};
	bool Ok = true;
	for (const FMutation& Mutation : Mutations)
	{
		FFixture Fixture;
		Ok &= TestTrue(TEXT("mutation fixture initialized"), Fixture.bReady);
		Ok &= TestTrue(TEXT("mutation anchor exists"), Fixture.Change(Mutation.File, Mutation.Before, Mutation.After));
		FCatalog Catalog;
		Catalog.Checksum = TEXT("previous identity");
		FString Error;
		Ok &= TestFalse(TEXT("invalid shared catalog rejected"), Fixture.Load(Catalog, Error));
		Ok &= TestTrue(TEXT("no partial snapshot published"), Catalog.Checksum.IsEmpty());
		Ok &= TestFalse(TEXT("rejection explains cause"), Error.IsEmpty());
	}
	{
		FFixture Fixture;
		IFileManager::Get().Delete(*FPaths::Combine(Fixture.Root, TEXT("profiles/motorcycle.json")), false, true);
		FCatalog Catalog; FString Error;
		Ok &= TestFalse(TEXT("missing fourth profile rejects entire snapshot"), Fixture.Load(Catalog, Error));
	}
	{
		FFixture Fixture;
		const FString Oversized = FString::ChrN(1024 * 1024 + 1, TEXT(' '));
		FFileHelper::SaveStringToFile(Oversized, *FPaths::Combine(Fixture.Root, TEXT("catalog.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FCatalog Catalog; FString Error;
		Ok &= TestFalse(TEXT("oversized file rejected before JSON parsing"), Fixture.Load(Catalog, Error));
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleAxleModuleCatalogTest,
	"DriveIntegration.Presentation.VehicleAxleModuleCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleAxleModuleCatalogTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	FFixture Fixture;
	FCatalog Original, Selected, Changed;
	FString Error;
	if (!TestTrue(TEXT("default module catalog loads"), Fixture.Load(Original, Error))) { AddError(Error); return false; }
	bool Ok = Fixture.Edit(TEXT("profiles/sedan.json"), [](const TSharedPtr<FJsonObject>& Profile)
	{
		const auto Modules = Profile->GetObjectField(TEXT("axle_modules"));
		Modules->GetObjectField(TEXT("front"))->SetStringField(TEXT("tire"), TEXT("tire_sedan_front_standard"));
		Modules->GetObjectField(TEXT("rear"))->SetStringField(TEXT("tire"), TEXT("tire_sedan_comfort"));
		Modules->GetObjectField(TEXT("rear"))->SetStringField(TEXT("suspension"), TEXT("suspension_sedan_comfort"));
	});
	Ok &= Fixture.Edit(TEXT("parts/tire_sedan_front_standard.json"), [](const TSharedPtr<FJsonObject>& Part)
	{
		Part->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("radius_m"), .36);
	});
	if (!TestTrue(TEXT("front and rear modules resolve"), Fixture.Load(Selected, Error))) { AddError(Error); return false; }
	Ok &= TestNotEqual(TEXT("module selection changes negotiated identity"), Selected.Checksum, Original.Checksum);
	const FProfile& Profile = Selected.Profiles[0];
	Ok &= TestEqual(TEXT("front tire radius resolves independently"), Profile.PlayerAxleTireRadiusMeters[0].Get(0), .36f);
	Ok &= TestEqual(TEXT("rear tire radius resolves independently"), Profile.PlayerAxleTireRadiusMeters[1].Get(0), .32f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ok &= TestTrue(TEXT("NPC wheel baseline remains unchanged"), Profile.WheelScales[Index].Equals(Original.Profiles[0].WheelScales[Index], 0));
		Ok &= TestTrue(TEXT("NPC wheel origin remains unchanged"), Profile.WheelOriginsCm[Index].Equals(Original.Profiles[0].WheelOriginsCm[Index], 0));
		const double MeshRadius = .29; // Deliberately differs from any selected tire.
		const FVector PlayerScale = Profile.PlayerWheelScale(Index, MeshRadius);
		Ok &= TestTrue(TEXT("rendered mesh radius equals the chosen axle module"),
			FMath::IsNearlyEqual(PlayerScale.Z * MeshRadius, static_cast<double>(Index < 2 ? .36f : .32f), 1e-8));
		Ok &= TestEqual(TEXT("radius edit does not change wheel width"), PlayerScale.Y, Profile.WheelScales[Index].Y);
		Ok &= TestEqual(TEXT("contact radius equals selected mesh radius"), Profile.PlayerWheelRadiusMeters(Index, MeshRadius), Index < 2 ? .36f : .32f);
	}
	Ok &= Fixture.Edit(TEXT("parts/suspension_sedan_standard.json"), [](const TSharedPtr<FJsonObject>& Part)
	{
		Part->SetStringField(TEXT("name"), TEXT("Unused suspension edit still changes identity"));
	});
	Ok &= TestTrue(TEXT("unselected valid part edit loads"), Fixture.Load(Changed, Error));
	Ok &= TestNotEqual(TEXT("every listed part participates in checksum"), Changed.Checksum, Selected.Checksum);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleAxleModuleValidationTest,
	"DriveIntegration.Presentation.VehicleAxleModuleStrictValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleAxleModuleValidationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using FEdit = TFunction<void(const TSharedPtr<FJsonObject>&)>;
	struct FCase { const TCHAR* File; FEdit Edit; };
	const FCase Cases[] = {
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->SetStringField(TEXT("axle_modules"), TEXT("invalid")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->RemoveField(TEXT("rear")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("front"))->SetStringField(TEXT("tire"), TEXT("unknown_tire")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("front"))->SetStringField(TEXT("tire"), TEXT("suspension_sedan_standard")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("rear"))->SetNumberField(TEXT("suspension"), 1); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("front"))->SetBoolField(TEXT("tyre"), true); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->SetStringField(TEXT("id"), TEXT("sedan_standard")); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->SetStringField(TEXT("id"), TEXT("tire_sedan_front_standard")); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->SetStringField(TEXT("kind"), TEXT("engine")); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("radius_m"), -.1); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("rim_diameter_m"), 1); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetStringField(TEXT("friction_coefficient"), TEXT(".95")); }},
		{TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->GetObjectField(TEXT("mass"))->SetNumberField(TEXT("mass_kg"), -1); }},
		{TEXT("parts/suspension_sedan_standard.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("max_compression_m"), 1); }},
		{TEXT("catalog.json"), [](const auto& P) { P->SetArrayField(TEXT("parts"), {MakeShared<FJsonValueString>(TEXT("../escaped.json"))}); }},
		{TEXT("catalog.json"), [](const auto& P) { P->SetArrayField(TEXT("parts"), {MakeShared<FJsonValueString>(TEXT("parts//tire.json"))}); }},
		{TEXT("catalog.json"), [](const auto& P) { P->SetArrayField(TEXT("parts"), {MakeShared<FJsonValueString>(TEXT("parts/missing.json"))}); }},
		{TEXT("catalog.json"), [](const auto& P) { auto Paths = P->GetArrayField(TEXT("parts")); const auto Duplicate = Paths[0]; Paths.Add(Duplicate); P->SetArrayField(TEXT("parts"), Paths); }},
		{TEXT("catalog.json"), [](const auto& P) { P->SetBoolField(TEXT("parts"), true); }},
		{TEXT("catalog.json"), [](const auto& P) { TArray<TSharedPtr<FJsonValue>> Paths; for (int32 I = 0; I < 65; ++I) Paths.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("parts/%d.json"), I))); P->SetArrayField(TEXT("parts"), Paths); }},
	};
	bool Ok = true;
	for (const FCase& Case : Cases)
	{
		FFixture Fixture;
		Ok &= TestTrue(TEXT("invalid module fixture written"), Fixture.Edit(Case.File, Case.Edit));
		FCatalog Catalog;
		FString Error;
		Ok &= TestFalse(TEXT("malformed module catalog rejected"), Fixture.Load(Catalog, Error));
		Ok &= TestTrue(TEXT("no partial module catalog published"), Catalog.Checksum.IsEmpty());
		Ok &= TestFalse(TEXT("module rejection explains cause"), Error.IsEmpty());
	}
	{
		FFixture Fixture;
		bool Edited = Fixture.Edit(TEXT("catalog.json"), [](const auto& P) { P->RemoveField(TEXT("parts")); P->RemoveField(TEXT("loadouts")); });
		for (const TCHAR* File : {TEXT("profiles/sedan.json"), TEXT("profiles/compact.json"), TEXT("profiles/truck.json"), TEXT("profiles/motorcycle.json")})
			Edited &= Fixture.Edit(File, [](const auto& P) { P->RemoveField(TEXT("axle_modules")); });
		FCatalog Legacy;
		FString Error;
		Ok &= TestTrue(TEXT("legacy optional-field fixture written"), Edited);
		Ok &= TestTrue(TEXT("catalog without parts or axle selections still loads"), Fixture.Load(Legacy, Error));
		Ok &= TestFalse(TEXT("legacy front radius stays inherited"), Legacy.Profiles[0].PlayerAxleTireRadiusMeters[0].IsSet());
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleAxleRadiusPresentationTest,
	"DriveIntegration.Presentation.VehicleAxleRadiusGroundAndSpin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleAxleRadiusPresentationTest::RunTest(const FString& Parameters)
{
	SimCoreProtocol::FVehicleState State;
	State.SpeedMps = 6.0f;
	State.PositionEnu = FVector3d(0, 0, .6);
	TStaticArray<float, 4> Radii;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Radii[Index] = Index < 2 ? .3f : .4f;
		auto& Wheel = State.Wheels.AddDefaulted_GetRef();
		Wheel.WheelIndex = Index;
		Wheel.bInContact = true;
		Wheel.ContactNormalEnu = FVector3d::UpVector;
		Wheel.ContactPointEnu = FVector3d::ZeroVector;
		Wheel.AngularSpeedRad = 1000.0f; // Exercise separate axle road-speed bounds.
	}
	const auto Sample = SimCorePresentation::BuildVehicleSample(State, 0, 0, .1f, .05f, .32f, FVector::ZeroVector, &Radii);
	bool Ok = true;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ok &= TestTrue(TEXT("axle contact retained"), Sample.Wheels[Index].bHasGroundContact);
		Ok &= TestTrue(TEXT("wheel center uses its axle radius"),
			FMath::IsNearlyEqual(Sample.Wheels[Index].RelativeCenterLocationCm.Z, (static_cast<double>(Radii[Index]) - .6) * 100, 1e-5));
	}
	Ok &= TestTrue(TEXT("front spin bounded by front radius"), FMath::IsNearlyEqual(Sample.FrontAxleAngularSpeedRadPerSecond, 20.6f, 1e-4f));
	Ok &= TestTrue(TEXT("rear spin bounded by rear radius"), FMath::IsNearlyEqual(Sample.RearAxleAngularSpeedRadPerSecond, 15.45f, 1e-4f));
	for (auto& Wheel : State.Wheels) Wheel.bInContact = false;
	const auto Airborne = SimCorePresentation::BuildVehicleSample(State, 0, 0, .1f, .05f, .32f, FVector::ZeroVector, &Radii);
	Ok &= TestTrue(TEXT("front fallback spin uses front radius"), FMath::IsNearlyEqual(Airborne.FrontAxleAngularSpeedRadPerSecond, 20.f, 1e-4f));
	Ok &= TestTrue(TEXT("rear fallback spin uses rear radius"), FMath::IsNearlyEqual(Airborne.RearAxleAngularSpeedRadPerSecond, 15.f, 1e-4f));
	for (float& Radius : Radii) Radius = 0.0f;
	const auto MissingAssets = SimCorePresentation::BuildVehicleSample(State, 0, 0, .1f, .05f, .32f, FVector::ZeroVector, &Radii);
	Ok &= TestTrue(TEXT("uninitialized front radii preserve the existing asset fallback"),
		FMath::IsNearlyEqual(MissingAssets.FrontAxleAngularSpeedRadPerSecond, 18.75f, 1e-4f));
	Ok &= TestTrue(TEXT("uninitialized rear radii preserve the existing asset fallback"),
		FMath::IsNearlyEqual(MissingAssets.RearAxleAngularSpeedRadPerSecond, 18.75f, 1e-4f));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehiclePowertrainCatalogTest,
	"DriveIntegration.Presentation.VehiclePowertrainCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehiclePowertrainCatalogTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	FFixture Fixture;
	FCatalog Baseline, Selected, Changed;
	FString Error;
	if (!TestTrue(TEXT("default null powertrain catalog loads"), Fixture.Load(Baseline, Error)))
	{
		AddError(Error);
		return false;
	}
	bool Ok = true;
	for (const TCHAR* File : {TEXT("profiles/sedan.json"), TEXT("profiles/compact.json"),
		TEXT("profiles/truck.json"), TEXT("profiles/motorcycle.json")})
	{
		Ok &= TestTrue(TEXT("default field remains explicitly null"), Fixture.Edit(File, [this, &Ok](const auto& Profile)
		{
			const auto Value = Profile->TryGetField(TEXT("powertrain_modules"));
			Ok &= TestTrue(TEXT("powertrain module default is null"), Value.IsValid() && Value->Type == EJson::Null);
			Profile->RemoveField(TEXT("powertrain_modules"));
		}));
	}
	Ok &= TestTrue(TEXT("absent powertrain field preserves legacy loading"), Fixture.Load(Changed, Error));
	Ok &= TestTrue(TEXT("complete selected powertrain fixture written"), Fixture.Edit(TEXT("profiles/sedan.json"), [](const auto& Profile)
	{
		Profile->SetObjectField(TEXT("powertrain_modules"), PowertrainSelection());
	}));
	if (!TestTrue(TEXT("valid engine gearbox drivetrain and tank combination loads"), Fixture.Load(Selected, Error)))
	{
		AddError(Error);
		return false;
	}
	Ok &= TestNotEqual(TEXT("powertrain selection changes negotiated identity"), Selected.Checksum, Baseline.Checksum);
	for (int32 Vehicle = 0; Vehicle < 4; ++Vehicle)
		for (int32 Wheel = 0; Wheel < 4; ++Wheel)
		{
			Ok &= TestTrue(TEXT("powertrain choice leaves wheel presentation unchanged"),
				Selected.Profiles[Vehicle].WheelScales[Wheel].Equals(Baseline.Profiles[Vehicle].WheelScales[Wheel], 0));
			Ok &= TestTrue(TEXT("powertrain choice leaves authored wheel origins unchanged"),
				Selected.Profiles[Vehicle].WheelOriginsCm[Wheel].Equals(Baseline.Profiles[Vehicle].WheelOriginsCm[Wheel], 0));
		}
	Ok &= TestTrue(TEXT("zero initial fuel fixture written"), Fixture.Edit(TEXT("profiles/sedan.json"), [](const auto& Profile)
	{
		Profile->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("initial_fuel_l"), 0);
	}));
	Ok &= TestTrue(TEXT("empty initial fuel tank is a valid selection"), Fixture.Load(Changed, Error));
	Ok &= TestNotEqual(TEXT("initial fuel participates in identity"), Changed.Checksum, Selected.Checksum);
	const FString BeforePartEdit = Changed.Checksum;
	Ok &= TestTrue(TEXT("engine part edit fixture written"), Fixture.Edit(TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& Part)
	{
		Part->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("response_time_s"), .2);
	}));
	Ok &= TestTrue(TEXT("valid engine part edit loads"), Fixture.Load(Changed, Error));
	Ok &= TestNotEqual(TEXT("engine part bytes participate in shared identity"), Changed.Checksum, BeforePartEdit);
	Ok &= Fixture.Edit(TEXT("profiles/sedan.json"), [](const auto& Profile)
	{
		Profile->SetField(TEXT("powertrain_modules"), MakeShared<FJsonValueNull>());
	});
	Ok &= Fixture.Edit(TEXT("parts/transmission_sedan_automatic_6speed.json"), [](const auto& Part)
	{
		Part->GetObjectField(TEXT("specification"))->SetArrayField(TEXT("forward_ratios"),
			{MakeShared<FJsonValueNumber>(4), MakeShared<FJsonValueNumber>(3.99999999)});
	});
	Ok &= Fixture.Edit(TEXT("catalog.json"), [](const auto& Manifest) { Manifest->RemoveField(TEXT("loadouts")); });
	Ok &= TestTrue(TEXT("unselected part retains the authoring schema's double precision"), Fixture.Load(Changed, Error));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehiclePowertrainValidationTest,
	"DriveIntegration.Presentation.VehiclePowertrainStrictValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehiclePowertrainValidationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using FEdit = TFunction<void(const TSharedPtr<FJsonObject>&)>;
	struct FCase { const TCHAR* File; FEdit Edit; };
	const FCase Cases[] = {
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->SetBoolField(TEXT("powertrain_modules"), true); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->RemoveField(TEXT("engine")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->RemoveField(TEXT("shift_duration_s")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("extra"), 1); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetStringField(TEXT("engine"), TEXT("missing_engine")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetStringField(TEXT("engine"), TEXT("tire_sedan_front_standard")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetStringField(TEXT("transmission"), TEXT("engine_sedan_gasoline_2_0")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetField(TEXT("fuel_tank"), MakeShared<FJsonValueNull>()); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("initial_fuel_l"), 51); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("initial_fuel_l"), 50.00000001); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("initial_fuel_l"), -1); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("downshift_rpm"), 800); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("downshift_rpm"), 5200); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("upshift_rpm"), 6500); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("shift_duration_s"), 0); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("shift_duration_s"), 11); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("shift_duration_s"), 1e-100); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetStringField(TEXT("initial_fuel_l"), TEXT("40")); }},
		{TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("front"))->SetStringField(TEXT("suspension"), TEXT("engine_sedan_gasoline_2_0")); }},
		{TEXT("parts/fuel_tank_sedan_50l.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetStringField(TEXT("fuel_type"), TEXT("diesel")); }},
		{TEXT("parts/fuel_tank_sedan_50l.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("fuel_density_kg_l"), 0); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->RemoveField(TEXT("torque_curve")); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetStringField(TEXT("fuel_type"), TEXT("electric")); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("rotational_inertia_kg_m2"), 0); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("idle_fuel_lph"), 1e-100); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->GetArrayField(TEXT("torque_curve"))[0]->AsObject()->SetNumberField(TEXT("torque_nm"), 1e-100); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->GetArrayField(TEXT("torque_curve"))[2]->AsObject()->SetNumberField(TEXT("rpm"), 1500.00001); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->GetArrayField(TEXT("torque_curve"))[0]->AsObject()->RemoveField(TEXT("rpm")); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->GetArrayField(TEXT("torque_curve"))[1]->AsObject()->SetNumberField(TEXT("rpm"), 800); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->GetArrayField(TEXT("torque_curve"))[0]->AsObject()->SetNumberField(TEXT("rpm"), 799); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { for (const auto& Point : P->GetObjectField(TEXT("specification"))->GetArrayField(TEXT("torque_curve"))) Point->AsObject()->SetNumberField(TEXT("torque_nm"), 0); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetArrayField(TEXT("torque_curve"), {MakeShared<FJsonValueNull>(), MakeShared<FJsonValueNull>()}); }},
		{TEXT("parts/engine_sedan_gasoline_2_0.json"), [](const auto& P) { const auto Spec = P->GetObjectField(TEXT("specification")); const auto Point = Spec->GetArrayField(TEXT("torque_curve"))[0]; TArray<TSharedPtr<FJsonValue>> Points; for (int32 Index = 0; Index < 129; ++Index) Points.Add(Point); Spec->SetArrayField(TEXT("torque_curve"), Points); }},
		{TEXT("parts/transmission_sedan_automatic_6speed.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("max_input_torque_nm"), 199); }},
		{TEXT("parts/transmission_sedan_automatic_6speed.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetArrayField(TEXT("forward_ratios"), {MakeShared<FJsonValueNumber>(2), MakeShared<FJsonValueNumber>(2)}); }},
		{TEXT("parts/transmission_sedan_automatic_6speed.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetArrayField(TEXT("forward_ratios"), {MakeShared<FJsonValueNumber>(4), MakeShared<FJsonValueNumber>(3.99999999)}); }},
		{TEXT("parts/transmission_sedan_automatic_6speed.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("efficiency"), 1.1); }},
		{TEXT("parts/drivetrain_sedan_rwd.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("front_torque_fraction"), 1.1); }},
		{TEXT("parts/drivetrain_sedan_rwd.json"), [](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(TEXT("front_torque_fraction"), 1e-100); }},
	};
	bool Ok = true;
	for (const FCase& Case : Cases)
	{
		FFixture Fixture;
		if (!TestTrue(TEXT("valid powertrain fixture initialized"), Fixture.Edit(TEXT("profiles/sedan.json"), [](const auto& P)
			{ P->SetObjectField(TEXT("powertrain_modules"), PowertrainSelection()); }))) return false;
		if (!TestTrue(TEXT("malformed powertrain fixture written"), Fixture.Edit(Case.File, Case.Edit))) return false;
		FCatalog Catalog;
		Catalog.Checksum = TEXT("previous identity");
		FString Error;
		Ok &= TestFalse(TEXT("malformed powertrain rejected"), Fixture.Load(Catalog, Error));
		Ok &= TestTrue(TEXT("no partial powertrain catalog published"), Catalog.Checksum.IsEmpty());
		Ok &= TestFalse(TEXT("powertrain rejection explains cause"), Error.IsEmpty());
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleLoadoutCatalogTest,
	"DriveIntegration.Presentation.VehicleLoadoutCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleLoadoutCatalogTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using SimCoreProtocol::ERuntimeVehicleClass;
	FFixture Fixture;
	FCatalog Catalog, Changed;
	FString Error;
	if (!TestTrue(TEXT("preset catalog loads"), Fixture.Load(Catalog, Error))) { AddError(Error); return false; }
	bool Ok = TestEqual(TEXT("two authored presets available"), Catalog.Loadouts.Num(), 2);
	FProfile Baseline, Standard, Comfort;
	Ok &= TestTrue(TEXT("empty selection resolves baseline"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT(""), Baseline));
	Ok &= TestTrue(TEXT("standard preset resolves"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_standard"), Standard));
	Ok &= TestTrue(TEXT("comfort preset resolves"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_comfort"), Comfort));
	Ok &= TestFalse(TEXT("baseline has no axle override"), Baseline.PlayerAxleTireRadiusMeters[0].IsSet());
	Ok &= TestEqual(TEXT("standard selected tire radius"), Standard.PlayerAxleTireRadiusMeters[0].Get(0), .32f);
	Ok &= TestEqual(TEXT("all eight component descriptions exposed"), Standard.ModuleDetails.Num(), 8);
	Ok &= TestTrue(TEXT("UI includes part mass and tire radius"), Standard.ModuleDetails[0].Contains(TEXT("kg")) && Standard.ModuleDetails[0].Contains(TEXT("radius")));
	Ok &= TestFalse(TEXT("preset cannot cross vehicle class"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Truck, TEXT("sedan_modular_standard"), Standard));
	Ok &= TestFalse(TEXT("unknown preset cannot fall back silently"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT("missing"), Standard));
	Ok &= Fixture.Edit(TEXT("loadouts/sedan_modular_comfort.json"), [](const auto& P) { P->SetStringField(TEXT("name"), TEXT("Comfort renamed")); });
	Ok &= TestTrue(TEXT("valid preset edit loads"), Fixture.Load(Changed, Error));
	Ok &= TestNotEqual(TEXT("preset raw bytes participate in identity"), Changed.Checksum, Catalog.Checksum);
	Ok &= Fixture.Edit(TEXT("profiles/sedan.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("front"))->SetStringField(TEXT("tire"), TEXT("tire_sedan_front_standard")); });
	Ok &= Fixture.Edit(TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P)
	{
		for (const TCHAR* Axle : {TEXT("front"), TEXT("rear")})
			for (const TCHAR* Kind : {TEXT("tire"), TEXT("suspension")})
				P->GetObjectField(TEXT("axle_modules"))->GetObjectField(Axle)->SetField(Kind, MakeShared<FJsonValueNull>());
		P->SetField(TEXT("powertrain_modules"), MakeShared<FJsonValueNull>());
	});
	Ok &= TestTrue(TEXT("explicit null preset loads"), Fixture.Load(Changed, Error));
	Ok &= TestTrue(TEXT("profile override remains selected"), Changed.Profiles[0].PlayerAxleTireRadiusMeters[0].IsSet());
	Ok &= TestTrue(TEXT("null preset resolves"), ResolveFromCatalog(Changed, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_standard"), Standard));
	Ok &= TestFalse(TEXT("null preset does not inherit profile override"), Standard.PlayerAxleTireRadiusMeters[0].IsSet());
	Ok &= Fixture.Edit(TEXT("catalog.json"), [](const auto& P) { P->RemoveField(TEXT("loadouts")); });
	Ok &= TestTrue(TEXT("legacy catalog without presets loads"), Fixture.Load(Changed, Error));
	Ok &= TestTrue(TEXT("no implicit presets added"), Changed.Loadouts.IsEmpty());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleLoadoutValidationTest,
	"DriveIntegration.Presentation.VehicleLoadoutStrictValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleLoadoutValidationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using FEdit = TFunction<void(const TSharedPtr<FJsonObject>&)>;
	struct FCase { const TCHAR* File; FEdit Edit; };
	const FCase Cases[] = {
		{TEXT("catalog.json"), [](const auto& P) { P->SetBoolField(TEXT("loadouts"), true); }},
		{TEXT("catalog.json"), [](const auto& P) { P->SetArrayField(TEXT("loadouts"), {MakeShared<FJsonValueString>(TEXT("../escape.json"))}); }},
		{TEXT("catalog.json"), [](const auto& P) { P->SetArrayField(TEXT("loadouts"), {MakeShared<FJsonValueString>(TEXT("loadouts/missing.json"))}); }},
		{TEXT("catalog.json"), [](const auto& P) { auto Paths = P->GetArrayField(TEXT("loadouts")); const auto Duplicate = Paths[0]; Paths.Add(Duplicate); P->SetArrayField(TEXT("loadouts"), Paths); }},
		{TEXT("catalog.json"), [](const auto& P) { TArray<TSharedPtr<FJsonValue>> Paths; for (int32 I = 0; I < 65; ++I) Paths.Add(MakeShared<FJsonValueString>(TEXT("loadouts/a.json"))); P->SetArrayField(TEXT("loadouts"), Paths); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->SetStringField(TEXT("id"), TEXT("sedan_standard")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->SetStringField(TEXT("id"), TEXT("tire_sedan_front_standard")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->SetStringField(TEXT("id"), TEXT("sedan_modular_comfort")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->SetStringField(TEXT("id"), TEXT("parts_v1_reserved")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->SetNumberField(TEXT("vehicle_class"), 1.5); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->RemoveField(TEXT("powertrain_modules")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->RemoveField(TEXT("axle_modules")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->GetObjectField(TEXT("axle_modules"))->GetObjectField(TEXT("front"))->SetStringField(TEXT("tire"), TEXT("missing_tire")); }},
		{TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->GetObjectField(TEXT("powertrain_modules"))->SetNumberField(TEXT("initial_fuel_l"), 50.00000001); }},
	};
	bool Ok = true;
	for (const auto& Case : Cases)
	{
		FFixture Fixture; FCatalog Catalog; FString Error;
		Ok &= TestTrue(TEXT("invalid preset fixture written"), Fixture.Edit(Case.File, Case.Edit));
		Ok &= TestFalse(TEXT("malformed preset rejects snapshot"), Fixture.Load(Catalog, Error));
		Ok &= TestTrue(TEXT("no partial preset snapshot published"), Catalog.Checksum.IsEmpty() && Catalog.Loadouts.IsEmpty());
		Ok &= TestFalse(TEXT("preset rejection has reason"), Error.IsEmpty());
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleCustomPartsRoundTripTest,
	"DriveIntegration.Presentation.VehicleCustomPartsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleCustomPartsRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using SimCoreProtocol::ERuntimeVehicleClass;
	FFixture Fixture;
	FCatalog Catalog;
	FString Error, Id;
	if (!TestTrue(TEXT("custom selection catalog loads"), Fixture.Load(Catalog, Error))) { AddError(Error); return false; }
	const FString Checksum = Catalog.Checksum;
	FPartsDraft Standard, Draft, RoundTrip;
	FProfile Profile, DynamicProfile;
	bool Ok = TestTrue(TEXT("named preset initializes all eight editable slots"),
		MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_standard"), Standard, Error));
	if (!TestEqual(TEXT("draft has exactly eight slots"), Standard.PartIds.Num(), PartsSlotCount)) return false;
	Ok &= TestTrue(TEXT("unchanged draft validates"), ResolvePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Standard, Id, Profile, Error));
	Ok &= TestEqual(TEXT("unchanged draft keeps named preset ID"), Id, FString(TEXT("sedan_modular_standard")));
	Draft = Standard;
	Draft.PartIds[0] = TEXT("tire_sedan_comfort");
	Ok &= TestTrue(TEXT("independent front tire replacement validates"), ResolvePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Draft, Id, Profile, Error));
	Ok &= TestEqual(TEXT("golden ID matches the C++ server ASCII-sorted indices"), Id, FString(TEXT("parts_v1_01_0504070401080002")));
	Ok &= TestEqual(TEXT("custom ID remains bounded"), Id.Len(), 28);
	Ok &= TestTrue(TEXT("custom ID reconstructs the draft"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Id, RoundTrip, Error));
	Ok &= TestEqual(TEXT("base preset and its fuel/shift policy identity retained"), RoundTrip.BaseLoadoutId, Draft.BaseLoadoutId);
	Ok &= TestTrue(TEXT("all eight part IDs survive round trip"), RoundTrip.PartIds == Draft.PartIds);
	Ok &= TestTrue(TEXT("normal player/NPC/replay visual resolver accepts custom ID"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Id, DynamicProfile));
	Ok &= TestTrue(TEXT("dynamic profile describes the same selected modules"), DynamicProfile.ModuleDetails == Profile.ModuleDetails);
	Ok &= TestTrue(TEXT("dynamic resolver retains the selected axle radius"), DynamicProfile.PlayerAxleTireRadiusMeters[0] == Profile.PlayerAxleTireRadiusMeters[0]);
	for (int32 Slot = 0; Slot < PartsSlotCount; ++Slot)
	{
		const auto Choices = PartChoicesFromCatalog(Catalog, Draft, Slot);
		Ok &= TestFalse(TEXT("every slot has a readable label"), PartSlotName(Slot).IsEmpty());
		Ok &= TestFalse(TEXT("every current slot has registered choices"), Choices.IsEmpty());
		Ok &= TestEqual(TEXT("only axle choices expose baseline"), Choices.ContainsByPredicate([](const auto& Choice) { return Choice.Id.IsEmpty(); }), Slot < 4);
		for (const auto& Choice : Choices)
		{
			Ok &= TestFalse(TEXT("choice shows its name and specifications"), Choice.Name.IsEmpty() || Choice.Summary.IsEmpty());
			if (!Choice.Id.IsEmpty()) Ok &= TestTrue(TEXT("mass is not presented as integrated chassis mass"), Choice.Summary.Contains(TEXT("metadata only")));
		}
	}
	for (int32 Slot = 0; Slot < 4; ++Slot) Draft.PartIds[Slot].Reset();
	Ok &= TestTrue(TEXT("axles can explicitly restore scalar baseline"), ResolvePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Draft, Id, Profile, Error));
	Ok &= TestEqual(TEXT("baseline sentinel encodes as lowercase ff"), Id, FString(TEXT("parts_v1_01_ffffffff01080002")));
	Ok &= TestFalse(TEXT("front baseline has no visual module radius"), Profile.PlayerAxleTireRadiusMeters[0].IsSet());
	Ok &= TestFalse(TEXT("rear baseline has no visual module radius"), Profile.PlayerAxleTireRadiusMeters[1].IsSet());
	Ok &= TestTrue(TEXT("baseline sentinel round trips"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Id, RoundTrip, Error));
	Ok &= TestTrue(TEXT("named preset remains immutable after previews"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Standard.BaseLoadoutId, RoundTrip, Error));
	Ok &= TestTrue(TEXT("preview never rewrites base part references"), Standard.PartIds == RoundTrip.PartIds);
	Ok &= TestEqual(TEXT("selection does not change shared catalog identity"), Catalog.Checksum, Checksum);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleCustomPartsValidationTest,
	"DriveIntegration.Presentation.VehicleCustomPartsStrictValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleCustomPartsValidationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using SimCoreProtocol::ERuntimeVehicleClass;
	FFixture Fixture;
	FCatalog Catalog;
	FString Error, Id;
	if (!TestTrue(TEXT("custom validation catalog loads"), Fixture.Load(Catalog, Error))) { AddError(Error); return false; }
	const TCHAR* InvalidIds[] = {
		TEXT("parts_v1_01_0504070401080002x"), TEXT("parts_v1_1_0504070401080002"),
		TEXT("parts_v1_01-0504070401080002"), TEXT("parts_v1_gg_0504070401080002"),
		TEXT("parts_v1_40_0504070401080002"), TEXT("parts_v1_01_4004070401080002"),
		TEXT("parts_v1_01_FF04070401080002"), TEXT("parts_v1_01_05040704ff080002"),
		TEXT("parts_v1_01_0104070401080002"), TEXT("parts_v1_01_0504070406080002"),
		TEXT("parts_v1_01_0604070401080002"), // Same parts as base must use its named ID.
	};
	bool Ok = true;
	for (const TCHAR* InvalidId : InvalidIds)
	{
		FPartsDraft Draft;
		Draft.BaseLoadoutId = TEXT("previous");
		FProfile Profile;
		Ok &= TestFalse(TEXT("malformed, wrong-kind or noncanonical ID rejected"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, InvalidId, Draft, Error));
		Ok &= TestTrue(TEXT("failed decode publishes no partial draft"), Draft.BaseLoadoutId.IsEmpty() && Draft.PartIds.IsEmpty());
		Ok &= TestFalse(TEXT("failed decode supplies a reason"), Error.IsEmpty());
		Ok &= TestFalse(TEXT("visual resolver cannot silently fall back"), ResolveFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, InvalidId, Profile));
	}
	FPartsDraft Base, Draft;
	FProfile Profile;
	Ok &= TestFalse(TEXT("default must first select a modular named preset"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT(""), Draft, Error));
	Ok &= TestFalse(TEXT("custom selection cannot cross class"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Truck, TEXT("parts_v1_01_0504070401080002"), Draft, Error));
	if (!MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_standard"), Base, Error)) return false;
	for (int32 Case = 0; Case < 6; ++Case)
	{
		Draft = Base;
		if (Case == 0) Draft.PartIds.Pop();
		if (Case == 1) Draft.PartIds.Add(TEXT("tire_sedan_comfort"));
		if (Case == 2) Draft.PartIds[4].Reset();
		if (Case == 3) Draft.PartIds[1] = TEXT("engine_sedan_gasoline_2_0");
		if (Case == 4) Draft.BaseLoadoutId = TEXT("parts_v1_01_0504070401080002");
		if (Case == 5) Draft.PartIds[0] = TEXT("unregistered_tire");
		Id = TEXT("previous"); Profile.ModuleDetails.Add(TEXT("previous"));
		Ok &= TestFalse(TEXT("invalid draft rejected before encoding"), ResolvePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Draft, Id, Profile, Error));
		Ok &= TestTrue(TEXT("failed validation clears preview and ID"), Id.IsEmpty() && Profile.ModuleDetails.IsEmpty());
		Ok &= TestFalse(TEXT("invalid draft explains correction"), Error.IsEmpty());
	}
	Ok &= TestTrue(TEXT("invalid slot has no choices"), PartChoicesFromCatalog(Catalog, Base, -1).IsEmpty() && PartChoicesFromCatalog(Catalog, Base, 8).IsEmpty());
	Ok &= Fixture.Edit(TEXT("loadouts/sedan_modular_standard.json"), [](const auto& P) { P->SetField(TEXT("powertrain_modules"), MakeShared<FJsonValueNull>()); });
	Ok &= TestTrue(TEXT("legacy non-modular preset still loads"), Fixture.Load(Catalog, Error));
	Ok &= TestFalse(TEXT("non-modular base cannot invent a fuel/shift policy"), MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Base.BaseLoadoutId, Draft, Error));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVehicleCustomPartsSafetyTest,
	"DriveIntegration.Presentation.VehicleCustomPartsLoadAndGeometrySafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleCustomPartsSafetyTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleVisualProfile;
	using SimCoreProtocol::ERuntimeVehicleClass;
	struct FCase { const TCHAR* File; const TCHAR* Key; double Value; int32 Slot; const TCHAR* PartId; };
	const FCase Cases[] = {
		{TEXT("parts/tire_sedan_comfort.json"), TEXT("rated_load_n"), 10, 0, TEXT("tire_sedan_comfort")},
		{TEXT("parts/tire_sedan_comfort.json"), TEXT("radius_m"), 1.4, 0, TEXT("tire_sedan_comfort")},
		{TEXT("parts/tire_sedan_comfort.json"), TEXT("width_m"), 1.6, 0, TEXT("tire_sedan_comfort")},
		{TEXT("parts/tire_sedan_comfort.json"), TEXT("rolling_resistance_coefficient"), 1e-100, 0, TEXT("tire_sedan_comfort")},
		{TEXT("parts/suspension_sedan_comfort.json"), TEXT("max_force_n"), 10, 1, TEXT("suspension_sedan_comfort")},
		{TEXT("parts/suspension_sedan_comfort.json"), TEXT("spring_rate_n_per_m"), 1, 1, TEXT("suspension_sedan_comfort")},
	};
	bool Ok = true;
	for (const FCase& Case : Cases)
	{
		FFixture Fixture;
		Ok &= Fixture.Edit(TEXT("catalog.json"), [](const auto& P)
			{ P->SetArrayField(TEXT("loadouts"), {MakeShared<FJsonValueString>(TEXT("loadouts/sedan_modular_standard.json"))}); });
		Ok &= Fixture.Edit(Case.File, [&Case](const auto& P) { P->GetObjectField(TEXT("specification"))->SetNumberField(Case.Key, Case.Value); });
		FCatalog Catalog; FString Error, Id; FPartsDraft Draft; FProfile Profile;
		if (!TestTrue(TEXT("unselected part may be authored independently"), Fixture.Load(Catalog, Error))) { AddError(Error); return false; }
		if (!MakePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_standard"), Draft, Error)) return false;
		Draft.PartIds[Case.Slot] = Case.PartId;
		Ok &= TestFalse(TEXT("unsafe axle combination cannot produce a custom ID"), ResolvePartsDraftFromCatalog(Catalog, ERuntimeVehicleClass::Sedan, Draft, Id, Profile, Error));
		Ok &= TestFalse(TEXT("fit/load rejection explains why"), Error.IsEmpty());
	}
	FFixture Fixture;
	FCatalog Before, After;
	FString Error, BeforeId, AfterId;
	FPartsDraft Draft;
	FProfile BeforeProfile, AfterProfile;
	if (!Fixture.Load(Before, Error) || !MakePartsDraftFromCatalog(Before, ERuntimeVehicleClass::Sedan, TEXT("sedan_modular_standard"), Draft, Error)) return false;
	Draft.PartIds[0] = TEXT("tire_sedan_comfort");
	Ok &= ResolvePartsDraftFromCatalog(Before, ERuntimeVehicleClass::Sedan, Draft, BeforeId, BeforeProfile, Error);
	Ok &= Fixture.Edit(TEXT("parts/tire_sedan_comfort.json"), [](const auto& P) { P->GetObjectField(TEXT("mass"))->SetNumberField(TEXT("mass_kg"), 9000); });
	Ok &= TestTrue(TEXT("part mass metadata loads"), Fixture.Load(After, Error));
	Ok &= TestTrue(TEXT("metadata mass does not silently increase static wheel load"), ResolvePartsDraftFromCatalog(After, ERuntimeVehicleClass::Sedan, Draft, AfterId, AfterProfile, Error));
	Ok &= TestEqual(TEXT("part IDs unaffected by metadata mass"), AfterId, BeforeId);
	Ok &= TestEqual(TEXT("metadata does not move physical CG"), AfterProfile.CgHeightMeters, BeforeProfile.CgHeightMeters);
	Ok &= TestEqual(TEXT("metadata does not resize body collision"), AfterProfile.HalfHeightMeters, BeforeProfile.HalfHeightMeters);
	Ok &= TestTrue(TEXT("metadata does not resize selected wheel"), AfterProfile.PlayerAxleTireRadiusMeters[0] == BeforeProfile.PlayerAxleTireRadiusMeters[0]);
	Ok &= TestNotEqual(TEXT("metadata edit still changes catalog handshake identity"), After.Checksum, Before.Checksum);
	return Ok;
}
#endif
