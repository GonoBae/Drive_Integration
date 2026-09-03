#include "SimCoreSensorRig.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
	const TCHAR* Config = TEXT(R"JSON({"format_version":1,"sensors":[{"id":"front_camera","type":"camera","child_frame":"camera_front_optical","position_flu_m":[1.8,0,0.8],"rotation_flu_deg":[0,-5,0],"rate_hz":10},{"id":"roof_lidar","type":"lidar","child_frame":"lidar_roof","position_flu_m":[0.2,0,1.5],"rotation_flu_deg":[0,0,0],"rate_hz":20}]})JSON");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSensorRigConfigTest,
	"DriveIntegration.Sensors.ConfigAndAuthoritativeSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSensorRigConfigTest::RunTest(const FString& Parameters)
{
	TArray<SimCoreSensorRig::FSensorDefinition> Sensors; FString Error;
	bool bOk = TestTrue(TEXT("strict sensor config parses"),
		SimCoreSensorRig::ParseConfigJson(Config, Sensors, Error));
	bOk &= TestEqual(TEXT("two authored mounts"), Sensors.Num(), 2);
	if (Sensors.Num() != 2) return false;
	const FTransform Camera = SimCoreSensorRig::BuildMountTransform(Sensors[0]);
	bOk &= TestTrue(TEXT("FLU mount converts to unit-scale Unreal FRU centimeters"),
		Camera.GetLocation().Equals(FVector(180, 0, 80), 1.e-6));
	USimCoreSensorRigComponent* Rig = NewObject<USimCoreSensorRigComponent>();
	bOk &= TestTrue(TEXT("component accepts same config"), Rig->InitializeFromJson(Config, Error));
	SimCoreProtocol::FVehicleState State;
	State.MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
	State.PlaySessionId = TEXT("sensor-test-play");
	State.Sequence = 1; State.SimulationTimeNs = 1'000'000'000;
	bOk &= TestEqual(TEXT("first authority emits every sensor"), Rig->ObserveAuthoritativeState(State), 2);
	State.Sequence = 2; State.SimulationTimeNs += 50'000'000;
	bOk &= TestEqual(TEXT("50ms emits 20Hz lidar only"), Rig->ObserveAuthoritativeState(State), 1);
	State.Sequence = 3; State.SimulationTimeNs += 50'000'000;
	bOk &= TestEqual(TEXT("100ms emits camera and lidar"), Rig->ObserveAuthoritativeState(State), 2);
	bOk &= TestEqual(TEXT("duplicate sequence cannot emit twice"), Rig->ObserveAuthoritativeState(State), 0);
	bOk &= TestTrue(TEXT("metadata retains frame and world identities"),
		Rig->GetRecentMetadata().Last().ParentFrame == TEXT("base_link")
		&& Rig->GetRecentMetadata().Last().MapChecksum == State.MapPackageChecksum
		&& Rig->GetRecentMetadata().Last().SourceSequence == State.Sequence);
	FString Bad = FString(Config).Replace(TEXT("\"rate_hz\":10"), TEXT("\"rate_hz\":0"));
	bOk &= TestFalse(TEXT("invalid sensor rate fails closed"),
		SimCoreSensorRig::ParseConfigJson(Bad, Sensors, Error));
	return bOk;
}
#endif
