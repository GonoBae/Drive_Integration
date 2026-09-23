#include "SimCoreDriveReplay.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "Misc/AutomationTest.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCorePresentation.h"
#include "SimCoreVehicleVisualProfile.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriveRecordKeyTest,
	"DriveIntegration.Replay.RecordKeyAvoidsEditorF5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriveRecordKeyTest::RunTest(const FString& Parameters)
{
	const UInputSettings* Settings = UInputSettings::GetInputSettings();
	if (!TestNotNull(TEXT("project input settings"), Settings)) return false;
	TArray<FInputActionKeyMapping> Mappings;
	Settings->GetActionMappingByName(TEXT("DriveRecord"), Mappings);
	bool bOk = TestTrue(TEXT("R toggles recording without modifiers"), Mappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping)
		{
			return Mapping.Key == EKeys::R && !Mapping.bShift && !Mapping.bCtrl
				&& !Mapping.bAlt && !Mapping.bCmd;
		}));
	bOk &= TestFalse(TEXT("F5 is no longer a recording key"), Mappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::F5; }));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriveReplayRoundTripTest,
	"DriveIntegration.Replay.RecordCsvRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriveReplayRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreDriveReplay;
	FTrack Track;
	for (uint64 Index = 0; Index < 3; ++Index)
	{
		SimCoreProtocol::FVehicleState State;
		State.Sequence = 10 + Index; State.SimulationTimeNs = 1'000'000'000 + Index * 100'000'000;
		State.MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
		State.PlaySessionId = TEXT("replay-test-play");
		State.PositionEnu = FVector3d(Index * 2.0, Index, 0.5);
		State.LinearVelocityEnu = FVector3d(20.0, 10.0, 0.0);
		State.HeadingDegrees = Index == 0 ? 359.0f : 1.0f + Index;
		State.SpeedMps = 22.0f;
		State.CollisionHalfLengthMeters = 2.2f;
		State.CollisionHalfWidthMeters = 1.0f;
		State.CollisionHalfHeightMeters = 0.75f;
		State.RuntimeVehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Truck;
		TestTrue(TEXT("ordered authoritative frame captured"), Track.Capture(State));
		TestFalse(TEXT("duplicate sequence rejected"), Track.Capture(State));
	}
	const FString Csv = SerializeCsv(Track);
	FTrack Parsed; FString Error;
	const bool bParsed = ParseCsv(Csv, Parsed, Error);
	bool bOk = TestTrue(FString::Printf(TEXT("strict replay CSV parses: %s"), *Error), bParsed);
	bOk &= TestEqual(TEXT("roundtrip frame count"), Parsed.Frames.Num(), 3);
	bOk &= TestEqual(TEXT("roundtrip map identity"), Parsed.MapChecksum, Track.MapChecksum);
	bOk &= TestEqual(TEXT("roundtrip preserves the selected vehicle class"),
		Parsed.Frames.IsValidIndex(0) ? Parsed.Frames[0].RuntimeVehicleClass
			: SimCoreProtocol::ERuntimeVehicleClass::Unspecified,
		SimCoreProtocol::ERuntimeVehicleClass::Truck);
	SimCoreProtocol::FVehicleState Midpoint;
	bOk &= TestTrue(TEXT("midpoint samples"), Sample(Parsed, 0.05, Midpoint));
	bOk &= TestTrue(TEXT("position interpolates by simulation time"),
		Midpoint.PositionEnu.Equals(FVector3d(1.0, 0.5, 0.5), 1.e-6));
	bOk &= TestTrue(TEXT("heading wraps through zero rather than spinning backward"),
		Midpoint.HeadingDegrees < 1.0f || Midpoint.HeadingDegrees > 359.0f);
	bOk &= TestEqual(TEXT("sampled ghost retains the recorded vehicle class"),
		Midpoint.RuntimeVehicleClass,
		SimCoreProtocol::ERuntimeVehicleClass::Truck);
	FTrack Rejected;
	bOk &= TestFalse(TEXT("unordered persisted frames fail closed"),
		ParseCsv(Csv.Replace(TEXT("1100000000,11"), TEXT("1000000000,11")), Rejected, Error));
	const FString LegacyCsv = TEXT(
		"simcore-drive-replay-v1,fnv1a64:0123456789abcdef,legacy-play\n"
		"1000000000,10,0,0,0.5,20,10,0,359,0,0,22,2.2,1,0.75\n");
	FTrack Legacy;
	bOk &= TestTrue(TEXT("v1 replay remains readable"), ParseCsv(LegacyCsv, Legacy, Error));
	bOk &= TestEqual(TEXT("v1 replay defaults its ghost to sedan"),
		Legacy.Frames.IsValidIndex(0) ? Legacy.Frames[0].RuntimeVehicleClass
			: SimCoreProtocol::ERuntimeVehicleClass::Unspecified,
		SimCoreProtocol::ERuntimeVehicleClass::Sedan);
	Track.VehicleCatalogChecksum = TEXT("fnv1a64:0123456789abcdef");
	FTrack WithCatalog;
	bOk &= TestTrue(TEXT("catalog-aware v3 header parses"), ParseCsv(SerializeCsv(Track), WithCatalog, Error));
	bOk &= TestEqual(TEXT("recorded catalog identity retained"), WithCatalog.VehicleCatalogChecksum, Track.VehicleCatalogChecksum);
	bOk &= TestTrue(TEXT("same catalog may replay"), ValidateCatalogIdentity(WithCatalog, Track.VehicleCatalogChecksum, Error));
	bOk &= TestFalse(TEXT("changed catalog cannot silently reinterpret old poses"),
		ValidateCatalogIdentity(WithCatalog, TEXT("fnv1a64:ffffffffffffffff"), Error));
	bOk &= TestTrue(TEXT("old track remains explicitly unverifiable but readable"),
		ValidateCatalogIdentity(Legacy, Track.VehicleCatalogChecksum, Error));
	bOk &= TestTrue(TEXT("v1 replay defaults to the catalog loadout"), Legacy.VehicleLoadoutId.IsEmpty());
	const FString LegacyV2Row = TEXT("1000000000,10,0,0,0.5,20,10,0,359,0,0,22,2.2,1,0.75,3\n");
	for (const FString& Header : {
		FString(TEXT("simcore-drive-replay-v2,fnv1a64:0123456789abcdef,legacy-play\n")),
		FString(TEXT("simcore-drive-replay-v2,fnv1a64:0123456789abcdef,legacy-play,fnv1a64:0123456789abcdef\n"))})
	{
		FTrack LegacyV2;
		bOk &= TestTrue(TEXT("v2 replay with or without catalog remains readable"), ParseCsv(Header + LegacyV2Row, LegacyV2, Error));
		bOk &= TestTrue(TEXT("v2 replay defaults to the catalog loadout"), LegacyV2.VehicleLoadoutId.IsEmpty());
		bOk &= TestTrue(TEXT("v2 selected class is preserved"), LegacyV2.Frames.Num() == 1
			&& LegacyV2.Frames[0].RuntimeVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Truck);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriveReplayLoadoutIdentityTest,
	"DriveIntegration.Replay.ImmutableLoadoutIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriveReplayLoadoutIdentityTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreDriveReplay;
	SimCoreProtocol::FVehicleState State;
	State.Sequence = 1;
	State.SimulationTimeNs = 1'000'000'000;
	State.MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
	State.PlaySessionId = TEXT("loadout-replay-session");
	State.VehicleLoadoutId = TEXT("sedan_comfort");
	State.RuntimeVehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	State.CollisionHalfLengthMeters = 2.2f;
	State.CollisionHalfWidthMeters = 1.0f;
	State.CollisionHalfHeightMeters = .75f;
	FTrack Track;
	Track.VehicleCatalogChecksum = TEXT("fnv1a64:abcdef0123456789");
	bool Ok = TestTrue(TEXT("first loadout frame captured"), Track.Capture(State));
	++State.Sequence;
	State.SimulationTimeNs += 100'000'000;
	Ok &= TestTrue(TEXT("same loadout continues recording"), Track.MatchesRecordingIdentity(State));
	Ok &= TestTrue(TEXT("second loadout frame captured"), Track.Capture(State));
	const FString Csv = SerializeCsv(Track);
	FTrack Loaded;
	FString Error;
	Ok &= TestTrue(TEXT("v3 loadout recording roundtrips"), ParseCsv(Csv, Loaded, Error));
	Ok &= TestEqual(TEXT("authoritative loadout ID survives persistence"), Loaded.VehicleLoadoutId, State.VehicleLoadoutId);
	SimCoreProtocol::FVehicleState Sampled;
	Ok &= TestTrue(TEXT("loadout recording samples"), Sample(Loaded, .05, Sampled));
	Ok &= TestEqual(TEXT("ghost state receives recorded loadout"), Sampled.VehicleLoadoutId, State.VehicleLoadoutId);
	FTrack Other = Track;
	Other.VehicleLoadoutId = TEXT("sedan_sport");
	FTrack OtherLoaded;
	Ok &= TestTrue(TEXT("second loadout with same catalog roundtrips"), ParseCsv(SerializeCsv(Other), OtherLoaded, Error));
	Ok &= TestEqual(TEXT("runtime selection does not change catalog identity"), OtherLoaded.VehicleCatalogChecksum, Loaded.VehicleCatalogChecksum);
	Ok &= TestNotEqual(TEXT("same catalog retains distinct selected loadouts"), OtherLoaded.VehicleLoadoutId, Loaded.VehicleLoadoutId);
	Ok &= TestTrue(TEXT("catalog validation accepts either selection's catalog"),
		ValidateCatalogIdentity(OtherLoaded, Loaded.VehicleCatalogChecksum, Error));

	++State.Sequence;
	State.SimulationTimeNs += 100'000'000;
	State.VehicleLoadoutId = TEXT("sedan_sport");
	Ok &= TestFalse(TEXT("same-class loadout change signals recording finalization"), Track.MatchesRecordingIdentity(State));
	Ok &= TestFalse(TEXT("different loadout cannot enter the same track"), Track.Capture(State));
	State.VehicleLoadoutId = Track.VehicleLoadoutId;
	State.PlaySessionId = TEXT("new-loadout-session");
	Ok &= TestFalse(TEXT("new reset session signals recording finalization"), Track.MatchesRecordingIdentity(State));
	Ok &= TestFalse(TEXT("new reset session cannot enter the same track"), Track.Capture(State));
	State.PlaySessionId = Track.PlaySessionId;
	State.MapPackageChecksum = TEXT("fnv1a64:ffffffffffffffff");
	Ok &= TestFalse(TEXT("map change signals recording finalization"), Track.MatchesRecordingIdentity(State));
	Ok &= TestEqual(TEXT("session changes retain the completed track"), Track.Frames.Num(), 2);

	const FString InvalidIds[] = {TEXT("Sedan_comfort"), TEXT("1_sedan"), TEXT("sedan-comfort"),
		TEXT("sedan,comfort"), TEXT("sedan\ncomfort"), FString::ChrN(65, TEXT('a'))};
	for (const FString& Id : InvalidIds)
	{
		FTrack Invalid = Track;
		Invalid.VehicleLoadoutId = Id;
		Ok &= TestTrue(TEXT("writer rejects malformed loadout IDs"), SerializeCsv(Invalid).IsEmpty());
		FTrack Rejected = Track;
		Ok &= TestFalse(TEXT("parser rejects malformed loadout IDs"),
			ParseCsv(Csv.Replace(TEXT("sedan_comfort"), *Id), Rejected, Error));
		Ok &= TestTrue(TEXT("invalid loadout clears partial parsed track"),
			Rejected.Frames.IsEmpty() && Rejected.VehicleLoadoutId.IsEmpty());
		FTrack Empty;
		State.VehicleLoadoutId = Id;
		Ok &= TestFalse(TEXT("capture rejects malformed authoritative loadout ID"), Empty.Capture(State));
	}
	Track.Reset();
	Ok &= TestTrue(TEXT("reset clears previous loadout identity"), Track.VehicleLoadoutId.IsEmpty());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriveReplayCustomPartsCatalogIdentityTest,
	"DriveIntegration.Replay.CustomPartsRequireCatalogIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriveReplayCustomPartsCatalogIdentityTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreDriveReplay;
	const FString CatalogChecksum = TEXT("fnv1a64:abcdef0123456789");
	SimCoreProtocol::FVehicleState State;
	State.Sequence = 1;
	State.SimulationTimeNs = 1'000'000'000;
	State.MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
	State.PlaySessionId = TEXT("custom-parts-replay");
	State.VehicleLoadoutId = TEXT("parts_v1_00_0102030405060708");
	State.RuntimeVehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	State.CollisionHalfLengthMeters = 2.2f;
	State.CollisionHalfWidthMeters = 1.f;
	State.CollisionHalfHeightMeters = .75f;
	FTrack Track;
	Track.VehicleCatalogChecksum = CatalogChecksum;
	bool Ok = TestTrue(TEXT("custom recording captures with valid catalog identity"), Track.Capture(State));
	const FString Csv = SerializeCsv(Track);
	Ok &= TestFalse(TEXT("custom track with catalog identity serializes"), Csv.IsEmpty());
	FTrack Loaded;
	FString Error;
	Ok &= TestTrue(TEXT("custom track with catalog identity parses"), ParseCsv(Csv, Loaded, Error));
	Ok &= TestEqual(TEXT("custom part selection survives persistence"), Loaded.VehicleLoadoutId, State.VehicleLoadoutId);
	Ok &= TestTrue(TEXT("custom replay accepts the matching catalog"), ValidateCatalogIdentity(Loaded, CatalogChecksum, Error));
	Ok &= TestFalse(TEXT("custom replay rejects a different catalog's slot indices"),
		ValidateCatalogIdentity(Loaded, TEXT("fnv1a64:ffffffffffffffff"), Error));
	SimCoreProtocol::FVehicleState Sampled;
	Ok &= TestTrue(TEXT("custom track with catalog identity can be sampled"), Sample(Loaded, 0, Sampled));
	Ok &= TestEqual(TEXT("sample retains the custom part selection"), Sampled.VehicleLoadoutId, State.VehicleLoadoutId);

	for (const FString& MissingOrInvalid : {FString(), FString(TEXT("invalid-checksum"))})
	{
		FTrack Recording;
		Recording.VehicleCatalogChecksum = MissingOrInvalid;
		Ok &= TestFalse(TEXT("custom recording rejects missing or malformed catalog identity"), Recording.Capture(State));
		Ok &= TestTrue(TEXT("failed custom capture does not publish identity or frames"),
			Recording.Frames.IsEmpty() && Recording.VehicleLoadoutId.IsEmpty());
		FTrack Invalid = Track;
		Invalid.VehicleCatalogChecksum = MissingOrInvalid;
		Ok &= TestTrue(TEXT("custom CSV cannot omit or corrupt its catalog identity"), SerializeCsv(Invalid).IsEmpty());
		FTrack Rejected = Track;
		Ok &= TestFalse(TEXT("custom CSV parser rejects missing or malformed catalog identity"),
			ParseCsv(Csv.Replace(*CatalogChecksum, *MissingOrInvalid), Rejected, Error));
		Ok &= TestTrue(TEXT("invalid custom header clears any previously loaded track"),
			Rejected.Frames.IsEmpty() && Rejected.VehicleLoadoutId.IsEmpty() && Rejected.VehicleCatalogChecksum.IsEmpty());
		Ok &= TestFalse(TEXT("custom replay validation cannot substitute the current catalog for missing history"),
			ValidateCatalogIdentity(Invalid, CatalogChecksum, Error));
		Ok &= TestFalse(TEXT("custom replay rejection explains the catalog requirement"), Error.IsEmpty());
		Ok &= TestFalse(TEXT("in-memory custom sampling cannot bypass missing catalog identity"), Sample(Invalid, 0, Sampled));
	}
	for (const FString& LegacyId : {FString(), FString(TEXT("sedan_modular_standard"))})
	{
		FTrack Legacy;
		State.VehicleLoadoutId = LegacyId;
		Ok &= TestTrue(TEXT("default and named recording retain optional catalog compatibility"), Legacy.Capture(State));
		FTrack ParsedLegacy;
		Ok &= TestTrue(TEXT("default and named CSV retain optional catalog compatibility"),
			ParseCsv(SerializeCsv(Legacy), ParsedLegacy, Error));
		Ok &= TestTrue(TEXT("default and named replay retain optional catalog compatibility"),
			ValidateCatalogIdentity(ParsedLegacy, CatalogChecksum, Error));
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDriveReplayPoseOriginTest,
	"DriveIntegration.Replay.RecordedPlayerPoseMatchesModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDriveReplayPoseOriginTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreDriveReplay;
	using namespace SimCoreProtocol;
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DriveReplayPoseQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("replay test world"), World)) return false;
	ASimCoreNpcPresentationActor* Ghost = World->SpawnActor<ASimCoreNpcPresentationActor>();
	if (!TestNotNull(TEXT("replay presentation actor"), Ghost))
	{
		World->DestroyWorld(false);
		return false;
	}
	bool bOk = true;
	const FVector PresentationOffset(1250.0, -370.0, 420.0);
	const ERuntimeVehicleClass Classes[] = {ERuntimeVehicleClass::Sedan,
		ERuntimeVehicleClass::Compact, ERuntimeVehicleClass::Truck, ERuntimeVehicleClass::Motorcycle};
	// Flat, banked slope, and overturned/in-air poses must retain the recorded
	// CG transform. A world-Z lift or terrain snap would fail the latter cases.
	const FVector Attitudes[] = {FVector(0.0, 0.0, 0.0),
		FVector(359.0, 18.0, -12.0), FVector(90.0, -25.0, 150.0)};
	for (const ERuntimeVehicleClass VehicleClass : Classes)
	{
		SimCoreVehicleVisualProfile::FProfile Profile;
		bOk &= TestTrue(TEXT("recorded vehicle profile exists"),
			SimCoreVehicleVisualProfile::Resolve(VehicleClass, Profile));
		for (const FVector& Attitude : Attitudes)
		{
			FTrack Track;
			for (uint64 Index = 0; Index < 2; ++Index)
			{
				FVehicleState State;
				State.EntityKind = EEntityKind::EgoVehicle;
				State.Sequence = 100 + Index;
				State.SimulationTimeNs = 1'000'000'000 + Index * 100'000'000;
				State.MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
				State.PlaySessionId = TEXT("recorded-player-pose");
				State.PositionEnu = FVector3d(-98.305 + Index, 41.292 + Index,
					Profile.CgHeightMeters + (Attitude.IsZero() ? 0.0 : 3.0 + Index * 0.25));
				State.HeadingDegrees = Attitude.X;
				State.PitchDegrees = Attitude.Y;
				State.RollDegrees = Attitude.Z;
				State.RuntimeVehicleClass = VehicleClass;
				State.CollisionHalfLengthMeters = 3.1f;
				State.CollisionHalfWidthMeters = 1.05f;
				State.CollisionHalfHeightMeters = Profile.HalfHeightMeters;
				bOk &= TestTrue(TEXT("player CG snapshot recorded"), Track.Capture(State));
			}
			FTrack Loaded;
			FString Error;
			if (!TestTrue(TEXT("saved pose roundtrip"), ParseCsv(SerializeCsv(Track), Loaded, Error)))
			{
				bOk = false;
				continue;
			}
			FVehicleState Sampled;
			bOk &= TestTrue(TEXT("saved pose interpolates"), Sample(Loaded, 0.05, Sampled));
			const auto Player = SimCorePresentation::BuildVehicleSample(
				Sampled, 0.0f, 0.0f, 1.0f, 0.05f, 0.32f, PresentationOffset);
			const FTransform Expected(Player.ActorRotation, Player.ActorLocation);
			bOk &= TestTrue(TEXT("replay accepts player-origin pose"), Ghost->ApplySnapshot(
				Sampled, 0.0f, 0.0f, true, 0.0f, PresentationOffset,
				SimCoreNpcPresentation::EPoseOrigin::PlayerCenterOfMass));
			bOk &= TestTrue(TEXT("replay model uses the live player's CG transform"),
				Ghost->GetModelRoot()->GetComponentTransform().Equals(Expected, 0.001));
			bOk &= TestTrue(TEXT("replay does not apply NPC recentering"),
				Ghost->GetModelRoot()->GetRelativeLocation().IsNearlyZero(0.001));
			bOk &= TestEqual(TEXT("selected model retained"), Ghost->GetRuntimeVehicleClass(), VehicleClass);
			if (const UStaticMesh* Mesh = Ghost->GetBody()->GetStaticMesh())
			{
				const FBox Bounds = Mesh->GetBoundingBox();
				const FTransform Actual = Ghost->GetBody()->GetComponentTransform();
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					const FVector Local((Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
						(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
						(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
					bOk &= TestTrue(TEXT("every body corner matches recorded player placement"),
						Actual.TransformPosition(Local).Equals(Expected.TransformPosition(Local), 0.001));
				}
			}
			else bOk &= TestTrue(TEXT("authored replay body loaded"), false);

			// The same renderer must still recenter normal NPC snapshots.
			FVector NpcOffset;
			bOk &= TestTrue(TEXT("NPC reference offset resolves"),
				SimCoreNpcPresentation::BuildAuthoredModelOffset(Ghost->GetAuthoredBounds(),
					Profile.HalfHeightMeters, NpcOffset, Profile.NpcCollisionGroundClearanceMeters * 100.0f));
			bOk &= TestTrue(TEXT("live NPC default path still accepted"),
				Ghost->ApplySnapshot(Sampled, 0.0f, 0.0f, true, 0.0f, PresentationOffset));
			bOk &= TestTrue(TEXT("live NPC OBB alignment unchanged"),
				Ghost->GetModelRoot()->GetRelativeLocation().Equals(NpcOffset, 0.001));
		}
	}
	World->DestroyWorld(false);
	return bOk;
}
#endif
