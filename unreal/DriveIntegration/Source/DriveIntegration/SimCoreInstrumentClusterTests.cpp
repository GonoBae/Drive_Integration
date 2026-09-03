#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreInstrumentCluster.h"

#include "DriveIntegrationGameModeBase.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreInstrumentClusterDisplayTest,
	"DriveIntegration.Presentation.InstrumentCluster.DisplayContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreInstrumentClusterDisplayTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreInstrumentCluster;
	SimCoreProtocol::FVehicleState State;
	State.SpeedMps = -12.5f;
	State.EngineRpm = 2345.4f;
	State.FuelPercent = 73.6f;
	State.Gear = SimCoreProtocol::EVehicleGear::Reverse;

	FDisplayState Display = BuildDisplayState(
		ESimCoreConnectionState::Connected, true, State, 0.02, 0.10, true);
	bool Ok = true;
	Ok &= TestTrue(TEXT("fresh connected values are authoritative"), Display.bAuthoritative);
	Ok &= TestEqual(TEXT("reverse speed uses unsigned road-speed magnitude"), Display.SpeedText, FString(TEXT("045")));
	Ok &= TestEqual(TEXT("authoritative RPM is shown"), Display.RpmText, FString(TEXT("2345")));
	Ok &= TestEqual(TEXT("authoritative reverse gear is shown"), Display.GearText, FString(TEXT("R")));
	Ok &= TestEqual(TEXT("authoritative fuel is rounded for display"), Display.FuelText, FString(TEXT("74")));
	Ok &= TestTrue(TEXT("local side-brake command is visibly requested"), Display.bSideBrakeRequested);
	Ok &= TestEqual(TEXT("fresh status is explicit"), Display.StatusText, FString(TEXT("LIVE")));

	State.SpeedMps = 10.0f;
	State.LinearVelocityEnu = FVector3d(10.0, 3.0, 0.0);
	Display = BuildDisplayState(
		ESimCoreConnectionState::Connected, true, State, 0.02, 0.10, false);
	Ok &= TestEqual(TEXT("speedometer uses complete ground path speed during cornering"),
		Display.SpeedText, FString(TEXT("038")));
	State.LinearVelocityEnu = FVector3d::ZeroVector;

	Display = BuildDisplayState(
		ESimCoreConnectionState::Connected, true, State, 0.101, 0.10, false);
	Ok &= TestFalse(TEXT("stale data is never presented as authoritative"), Display.bAuthoritative);
	Ok &= TestEqual(TEXT("stale speed is blanked"), Display.SpeedText, FString(TEXT("---")));
	Ok &= TestEqual(TEXT("stale RPM is blanked"), Display.RpmText, FString(TEXT("----")));
	Ok &= TestEqual(TEXT("stale gear is blanked"), Display.GearText, FString(TEXT("-")));
	Ok &= TestEqual(TEXT("stale fuel is blanked"), Display.FuelText, FString(TEXT("--")));
	Ok &= TestEqual(TEXT("stale status is explicit"), Display.StatusText, FString(TEXT("STALE")));

	Display = BuildDisplayState(
		ESimCoreConnectionState::WaitingToReconnect, true, State, 0.01, 0.10, false);
	Ok &= TestFalse(TEXT("reconnect cannot reuse a previous authoritative snapshot"), Display.bAuthoritative);
	Ok &= TestEqual(TEXT("reconnect status is explicit"), Display.StatusText, FString(TEXT("RECONNECTING")));

	State.EngineRpm = std::numeric_limits<float>::quiet_NaN();
	Display = BuildDisplayState(
		ESimCoreConnectionState::Connected, true, State, 0.01, 0.10, false);
	Ok &= TestFalse(TEXT("non-finite telemetry fails closed"), Display.bAuthoritative);
	Ok &= TestEqual(TEXT("invalid status is explicit"), Display.StatusText, FString(TEXT("INVALID")));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreInstrumentClusterGameModeTest,
	"DriveIntegration.Presentation.InstrumentCluster.AutomaticGameModeInstall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreInstrumentClusterGameModeTest::RunTest(const FString& Parameters)
{
	const ADriveIntegrationGameModeBase* GameMode = GetDefault<ADriveIntegrationGameModeBase>();
	return TestTrue(
		TEXT("code-native cluster is installed without Blueprint setup"),
		GameMode != nullptr && GameMode->HUDClass == ASimCoreInstrumentClusterHud::StaticClass());
}

#endif
