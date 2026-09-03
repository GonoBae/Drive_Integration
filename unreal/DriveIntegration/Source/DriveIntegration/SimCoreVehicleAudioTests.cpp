#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SimCoreVehicleAudio.h"

namespace
{
SimCoreProtocol::FVehicleState MakeAudioState(float SpeedMps, float Rpm, bool bContact)
{
	SimCoreProtocol::FVehicleState State;
	State.SpeedMps = SpeedMps;
	State.EngineRpm = Rpm;
	for (uint32 Index = 0; Index < 4; ++Index)
	{
		SimCoreProtocol::FVehicleState::FWheelState Wheel;
		Wheel.WheelIndex = Index;
		Wheel.bInContact = bContact;
		Wheel.AngularSpeedRad = SpeedMps / 0.32f;
		Wheel.NormalLoadN = 3500.0f;
		State.Wheels.Add(Wheel);
	}
	return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVehicleAudioAuthoritativeInputsTest,
	"DriveIntegration.Presentation.VehicleAudio.AuthoritativeInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleAudioAuthoritativeInputsTest::RunTest(const FString& Parameters)
{
	const SimCoreVehicleAudio::FParameters Idle =
		SimCoreVehicleAudio::BuildParameters(MakeAudioState(0.0f, 800.0f, true), 0.01f, 0.10f);
	TestTrue(TEXT("fresh idle RPM produces engine sound"), Idle.EngineAmplitude > 0.0f);
	TestEqual(TEXT("parked tires remain silent"), Idle.TireAmplitude, 0.0f);

	const SimCoreVehicleAudio::FParameters Rolling =
		SimCoreVehicleAudio::BuildParameters(MakeAudioState(12.0f, 2800.0f, true), 0.01f, 0.10f);
	TestTrue(TEXT("RPM raises engine frequency"), Rolling.EngineFrequencyHz > Idle.EngineFrequencyHz);
	TestTrue(TEXT("contact plus road speed produces rolling friction"), Rolling.TireAmplitude > 0.0f);

	SimCoreProtocol::FVehicleState SlidingState = MakeAudioState(12.0f, 2800.0f, true);
	for (SimCoreProtocol::FVehicleState::FWheelState& Wheel : SlidingState.Wheels)
	{
		Wheel.SlipAngleRad = 0.24f;
	}
	const SimCoreVehicleAudio::FParameters Sliding =
		SimCoreVehicleAudio::BuildParameters(SlidingState, 0.01f, 0.10f);
	TestTrue(TEXT("published tire slip raises friction sound"), Sliding.TireAmplitude > Rolling.TireAmplitude);
	TestTrue(TEXT("published tire slip raises noise color"), Sliding.TireNoiseCutoffHz > Rolling.TireNoiseCutoffHz);

	const SimCoreVehicleAudio::FParameters Airborne =
		SimCoreVehicleAudio::BuildParameters(MakeAudioState(12.0f, 2800.0f, false), 0.01f, 0.10f);
	TestEqual(TEXT("airborne tires are silent"), Airborne.TireAmplitude, 0.0f);
	TestTrue(TEXT("airborne engine remains audible"), Airborne.EngineAmplitude > 0.0f);

	const SimCoreVehicleAudio::FParameters Stale =
		SimCoreVehicleAudio::BuildParameters(MakeAudioState(12.0f, 2800.0f, true), 0.25f, 0.10f);
	TestEqual(TEXT("stale state silences engine"), Stale.EngineAmplitude, 0.0f);
	TestEqual(TEXT("stale state silences tires"), Stale.TireAmplitude, 0.0f);
	return true;
}

#endif
