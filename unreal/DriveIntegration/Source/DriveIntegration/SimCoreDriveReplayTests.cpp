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
	return bOk;
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
