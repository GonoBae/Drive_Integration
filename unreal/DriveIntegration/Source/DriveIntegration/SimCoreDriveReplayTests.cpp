#include "SimCoreDriveReplay.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

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
		TestTrue(TEXT("ordered authoritative frame captured"), Track.Capture(State));
		TestFalse(TEXT("duplicate sequence rejected"), Track.Capture(State));
	}
	const FString Csv = SerializeCsv(Track);
	FTrack Parsed; FString Error;
	const bool bParsed = ParseCsv(Csv, Parsed, Error);
	bool bOk = TestTrue(FString::Printf(TEXT("strict replay CSV parses: %s"), *Error), bParsed);
	bOk &= TestEqual(TEXT("roundtrip frame count"), Parsed.Frames.Num(), 3);
	bOk &= TestEqual(TEXT("roundtrip map identity"), Parsed.MapChecksum, Track.MapChecksum);
	SimCoreProtocol::FVehicleState Midpoint;
	bOk &= TestTrue(TEXT("midpoint samples"), Sample(Parsed, 0.05, Midpoint));
	bOk &= TestTrue(TEXT("position interpolates by simulation time"),
		Midpoint.PositionEnu.Equals(FVector3d(1.0, 0.5, 0.5), 1.e-6));
	bOk &= TestTrue(TEXT("heading wraps through zero rather than spinning backward"),
		Midpoint.HeadingDegrees < 1.0f || Midpoint.HeadingDegrees > 359.0f);
	FTrack Rejected;
	bOk &= TestFalse(TEXT("unordered persisted frames fail closed"),
		ParseCsv(Csv.Replace(TEXT("1100000000,11"), TEXT("1000000000,11")), Rejected, Error));
	return bOk;
}
#endif
