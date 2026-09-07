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
		ESimCoreConnectionState::Connected, true, State, 0.02, 0.10, true,
		SimCoreProtocol::ETurnIndicator::Off, true);
	bool Ok = true;
	Ok &= TestTrue(TEXT("fresh connected values are authoritative"), Display.bAuthoritative);
	Ok &= TestEqual(TEXT("reverse speed uses unsigned road-speed magnitude"), Display.SpeedText, FString(TEXT("045")));
	Ok &= TestEqual(TEXT("authoritative RPM is shown"), Display.RpmText, FString(TEXT("2345")));
	Ok &= TestEqual(TEXT("authoritative reverse gear is shown"), Display.GearText, FString(TEXT("R")));
	Ok &= TestEqual(TEXT("authoritative fuel is rounded for display"), Display.FuelText, FString(TEXT("74")));
	Ok &= TestTrue(TEXT("local side-brake command is visibly requested"), Display.bSideBrakeRequested);
	Ok &= TestTrue(TEXT("X hazard request remains clearly visible on the instrument cluster"),
		Display.bHazardLightsRequested
		&& Display.ManualIndicator == SimCoreProtocol::ETurnIndicator::Off);
	Ok &= TestEqual(TEXT("fresh status is explicit"), Display.StatusText, FString(TEXT("LIVE")));

	State.SpeedMps = 10.0f;
	State.LinearVelocityEnu = FVector3d(10.0, 3.0, 0.0);
	Display = BuildDisplayState(
		ESimCoreConnectionState::Connected, true, State, 0.02, 0.10, false,
		SimCoreProtocol::ETurnIndicator::Left, false);
	Ok &= TestEqual(TEXT("speedometer uses complete ground path speed during cornering"),
		Display.SpeedText, FString(TEXT("038")));
	Ok &= TestTrue(TEXT("manual Q/E direction remains an explicit local dashboard state"),
		!Display.bHazardLightsRequested
		&& Display.ManualIndicator == SimCoreProtocol::ETurnIndicator::Left);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreInstrumentClusterViewportTest,
	"DriveIntegration.Presentation.InstrumentCluster.CompactViewportLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreInstrumentClusterViewportTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const FVector2D View : {FVector2D(640, 360), FVector2D(1280, 720),
		FVector2D(1920, 1080), FVector2D(3840, 2160), FVector2D(640, 480), FVector2D(2560, 1080)})
	{
		const auto Layout = SimCoreInstrumentCluster::BuildLayout(View.X, View.Y);
		const FString Resolution = FString::Printf(TEXT("%.0fx%.0f"), View.X, View.Y);
		bOk &= TestTrue(Resolution + TEXT(" cluster stays completely inside viewport"),
			Layout.Position.X >= 0 && Layout.Position.Y >= 0
			&& Layout.Size.X > 0 && Layout.Size.Y > 0
			&& Layout.Position.X + Layout.Size.X < View.X
			&& Layout.Position.Y + Layout.Size.Y < View.Y);
		bOk &= TestTrue(Resolution + TEXT(" cluster covers no more than six percent of screen"),
			Layout.Size.X * Layout.Size.Y / (View.X * View.Y) <= 0.06);
		bOk &= TestTrue(Resolution + TEXT(" cluster leaves the central driving view clear"),
			Layout.Position.X >= View.X * 0.65 && Layout.Position.Y >= View.Y * 0.70);
		bOk &= TestTrue(Resolution + TEXT(" cluster proportions and scale remain bounded"),
			FMath::IsNearlyEqual(Layout.Size.X / Layout.Size.Y, 350.0 / 136.0, 1.e-5)
			&& Layout.Scale > 0 && Layout.Scale <= 1.35f);
	}
	const USimCoreClientComponent* Client = GetDefault<USimCoreClientComponent>();
	bOk &= TestTrue(TEXT("detailed network diagnostics do not cover the viewport by default"),
		Client && !Client->bShowDebugHud);
	return bOk;
}

#endif
