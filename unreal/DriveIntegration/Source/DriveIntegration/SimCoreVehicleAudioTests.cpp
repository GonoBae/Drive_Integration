#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SimCoreVehicleAudio.h"
#include "SimCoreTurnSignals.h"

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
	TestEqual(TEXT("straight driving has no constant tire hiss"), Rolling.TireAmplitude, 0.0f);

	SimCoreProtocol::FVehicleState SlidingState = MakeAudioState(12.0f, 2800.0f, true);
	for (SimCoreProtocol::FVehicleState::FWheelState& Wheel : SlidingState.Wheels)
	{
		Wheel.SlipAngleRad = 0.30f;
	}
	const SimCoreVehicleAudio::FParameters Sliding =
		SimCoreVehicleAudio::BuildParameters(SlidingState, 0.01f, 0.10f);
	TestTrue(TEXT("published tire slip raises friction sound"), Sliding.TireAmplitude > Rolling.TireAmplitude);
	TestTrue(TEXT("published tire slip raises noise color"), Sliding.TireNoiseCutoffHz > Rolling.TireNoiseCutoffHz);
	TestTrue(TEXT("cornering slip has its own squeal, not just louder rolling hiss"), Sliding.TireSquealAmplitude > 0.05f);
	TestEqual(TEXT("straight rolling does not squeal"), Rolling.TireSquealAmplitude, 0.0f);

	const SimCoreVehicleAudio::FParameters Airborne =
		SimCoreVehicleAudio::BuildParameters(MakeAudioState(12.0f, 2800.0f, false), 0.01f, 0.10f);
	TestEqual(TEXT("airborne tires are silent"), Airborne.TireAmplitude, 0.0f);
	TestEqual(TEXT("airborne tires cannot squeal"), Airborne.TireSquealAmplitude, 0.0f);
	TestTrue(TEXT("airborne engine remains audible"), Airborne.EngineAmplitude > 0.0f);

	const SimCoreVehicleAudio::FParameters Stale =
		SimCoreVehicleAudio::BuildParameters(MakeAudioState(12.0f, 2800.0f, true), 0.25f, 0.10f);
	TestEqual(TEXT("stale state silences engine"), Stale.EngineAmplitude, 0.0f);
	TestEqual(TEXT("stale state silences tires"), Stale.TireAmplitude, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreIndicatorClickTest,
	"DriveIntegration.Presentation.VehicleAudio.IndicatorLampSynchronization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreIndicatorClickTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleAudio;
	using SimCoreProtocol::ETurnIndicator;
	for (const auto Direction : {ETurnIndicator::Left, ETurnIndicator::Right, ETurnIndicator::Off})
	{
		const bool bHazard = Direction == ETurnIndicator::Off;
		FIndicatorClicks Clicks;
		SimCoreTurnSignals::FPhaseClock Clock;
		int32 Count = 0;
		bool bPreviousLit = false;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			const double Time = Clock.Update(Direction, 17.395 + Frame / 60.0, bHazard);
			const bool bLit = USimCoreTurnSignals::IsLit(Direction, true, Time, bHazard)
				|| USimCoreTurnSignals::IsLit(Direction, false, Time, bHazard);
			const auto Edge = Clicks.Update(Direction, Time, bHazard);
			TestEqual(TEXT("one click only when visible relay state changes"), Edge != EIndicatorClick::None, bLit != bPreviousLit);
			if (Edge != EIndicatorClick::None)
			{
				++Count;
				TestEqual(TEXT("on and off have the corresponding timbre"), Edge == EIndicatorClick::On, bLit);
			}
			TestTrue(TEXT("duplicate frame cannot click twice"), Clicks.Update(Direction, Time, bHazard) == EIndicatorClick::None);
			bPreviousLit = bLit;
		}
		TestTrue(TEXT("repeating indicator is audible"), Count >= 10 && Count <= 12);
		TestTrue(TEXT("pause does not replay skipped clicks"), Clicks.Update(Direction, 20.0, bHazard) == EIndicatorClick::None);
		TestTrue(TEXT("clock reset has no artificial transition"), Clicks.Update(Direction, 0.0, bHazard) == EIndicatorClick::None);
		TestTrue(TEXT("switch off cancels quietly"), Clicks.Update(ETurnIndicator::Off, 0.1, false) == EIndicatorClick::None);
	}
	SimCoreTurnSignals::FPhaseClock Clock;
	FIndicatorClicks Clicks;
	TestTrue(TEXT("selection gets its first on-click even at the end of a global flash"),
		Clicks.Update(ETurnIndicator::Left, Clock.Update(ETurnIndicator::Left, 17.395, false), false) == EIndicatorClick::On);
	Clicks.Update(ETurnIndicator::Left, Clock.Update(ETurnIndicator::Left, 17.895, false), false);
	TestTrue(TEXT("direction change does not lose its first click to backward relative-time suppression"),
		Clicks.Update(ETurnIndicator::Right, Clock.Update(ETurnIndicator::Right, 17.9, false), false) == EIndicatorClick::On);
	TestTrue(TEXT("hazard selection starts one shared click, not one per lamp"),
		Clicks.Update(ETurnIndicator::Off, Clock.Update(ETurnIndicator::Off, 18.0, true), true) == EIndicatorClick::On);
	// A disconnected component still feeds its silent lamp baseline. Restoring
	// sound on the same phase must not invent an extra turn-signal transition.
	Clicks.Update(ETurnIndicator::Off, Clock.Update(ETurnIndicator::Off, 18.2, true), true);
	TestTrue(TEXT("reconnect during an existing flash does not click again"),
		Clicks.Update(ETurnIndicator::Off, Clock.Update(ETurnIndicator::Off, 18.21, true), true) == EIndicatorClick::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTireSoundRenderTest,
	"DriveIntegration.Presentation.VehicleAudio.TireSlipAndOfflineRender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreTireSoundRenderTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleAudio;
	auto StraightState = MakeAudioState(25.0f, 3200.0f, true);
	auto CornerState = StraightState;
	for (auto& Wheel : CornerState.Wheels) Wheel.SlipAngleRad = 0.30f;
	auto Straight = BuildParameters(StraightState, 0.01f, 0.1f);
	auto Corner = BuildParameters(CornerState, 0.01f, 0.1f);
	TestTrue(TEXT("high-speed lateral scrub produces a clear squeal layer"), Corner.TireSquealAmplitude >= 0.20f);
	for (auto& Wheel : CornerState.Wheels) Wheel.NormalLoadN = 0.0f;
	TestEqual(TEXT("contact flag without support load cannot make tire noise"),
		BuildParameters(CornerState, 0.01f, 0.1f).TireSquealAmplitude, 0.0f);
	auto ParkingState = MakeAudioState(0.5f, 900.0f, true);
	for (auto& Wheel : ParkingState.Wheels) Wheel.SlipAngleRad = 1.0f;
	TestEqual(TEXT("parking steering is not a high-speed skid"), BuildParameters(ParkingState, 0.01f, 0.1f).TireSquealAmplitude, 0.0f);
	Straight.EngineAmplitude = Corner.EngineAmplitude = 0.0f;
	for (int32 Rate : {8000, 48000, 96000})
	{
		TArray<float> RollingSamples, CornerSamples, RelaySamples;
		RollingSamples.SetNumZeroed(Rate * 2);
		CornerSamples.SetNumZeroed(Rate * 2);
		RelaySamples.SetNumZeroed(Rate / 5);
		FRenderer RollingVoice, CornerVoice, RelayVoice;
		RollingVoice.SetTargets(Straight); CornerVoice.SetTargets(Corner);
		RollingVoice.Render(RollingSamples.GetData(), RollingSamples.Num(), Rate);
		CornerVoice.Render(CornerSamples.GetData(), CornerSamples.Num(), Rate);
		RelayVoice.Click(EIndicatorClick::On);
		RelayVoice.Render(RelaySamples.GetData(), RelaySamples.Num(), Rate);
		double RollingEnergy = 0.0, CornerEnergy = 0.0, RelayEnergy = 0.0;
		bool bBoundedStereo = true;
		for (int32 Index = 0; Index < RollingSamples.Num(); Index += 2)
		{
			RollingEnergy += FMath::Square(RollingSamples[Index]);
			CornerEnergy += FMath::Square(CornerSamples[Index]);
			bBoundedStereo &= FMath::IsFinite(CornerSamples[Index]) && FMath::Abs(CornerSamples[Index]) < 0.92f
				&& CornerSamples[Index] == CornerSamples[Index + 1];
		}
		for (int32 Index = 0; Index < RelaySamples.Num(); ++Index) RelayEnergy += FMath::Square(RelaySamples[Index]);
		TestTrue(TEXT("rendered corner scrub is distinct from quiet road rumble"), CornerEnergy > RollingEnergy * 4.0);
		TestTrue(TEXT("stereo samples are finite, matched, and unclipped"), bBoundedStereo);
		// A pure sine plus its octave repeats almost perfectly one period later.
		// Rubber scrub must retain broad, irregular energy even at constant slip.
		double MaximumPeriodCorrelation = 0.0;
		for (int32 Lag = FMath::Max(1, FMath::FloorToInt(Rate / Corner.TireSquealFrequencyHz * 0.85f));
			Lag <= FMath::CeilToInt(Rate / Corner.TireSquealFrequencyHz * 1.15f); ++Lag)
		{
			double Product = 0.0, EnergyA = 0.0, EnergyB = 0.0;
			for (int32 Frame = Rate / 4; Frame < Rate - Lag; ++Frame)
			{
				const double A = CornerSamples[Frame * 2], B = CornerSamples[(Frame + Lag) * 2];
				Product += A * B; EnergyA += A * A; EnergyB += B * B;
			}
			MaximumPeriodCorrelation = FMath::Max(MaximumPeriodCorrelation,
				Product / FMath::Sqrt(FMath::Max(EnergyA * EnergyB, 1.e-12)));
		}
		TestTrue(TEXT("steady skid has friction texture, not a repeating alarm tone"), MaximumPeriodCorrelation < 0.60);
		const double CornerRms = FMath::Sqrt(CornerEnergy / Rate);
		TestTrue(TEXT("friction remains audible without harsh sample-rate-dependent gain"), CornerRms > 0.025 && CornerRms < 0.10);
		AddInfo(FString::Printf(TEXT("Tire texture: rate=%d RMS=%.4f period-correlation=%.4f"),
			Rate, CornerRms, MaximumPeriodCorrelation));
		TArray<float> SplitSamples;
		SplitSamples.SetNumZeroed(CornerSamples.Num());
		FRenderer SplitVoice;
		SplitVoice.SetTargets(Corner);
		const int32 Split = (SplitSamples.Num() / 3) & ~1;
		SplitVoice.Render(SplitSamples.GetData(), Split, Rate);
		SplitVoice.Render(SplitSamples.GetData() + Split, SplitSamples.Num() - Split, Rate);
		TestTrue(TEXT("audio buffer boundaries do not reset or repeat the friction texture"), SplitSamples == CornerSamples);
		TestTrue(TEXT("relay emits a short audible transient"), RelayEnergy > 0.001);
		TestEqual(TEXT("relay ends instead of becoming a looping tone"), RelaySamples.Last(), 0.0f);
		CornerVoice.SetTargets({});
		CornerVoice.Render(CornerSamples.GetData(), CornerSamples.Num(), Rate);
		TestTrue(TEXT("stale targets decay instead of sustaining a squeal"), FMath::Abs(CornerSamples.Last()) < 0.0001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTireSharpCornerOnlyTest,
	"DriveIntegration.Presentation.VehicleAudio.SharpCornerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreTireSharpCornerOnlyTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleAudio;
	for (const float Speed : {4.0f, 8.0f, 12.0f, 25.0f, 40.0f})
	{
		for (const float Angle : {0.0f, 0.04f, 0.08f, 0.12f, 0.169f})
		{
			auto State = MakeAudioState(Speed, 2800.0f, true);
			for (auto& Wheel : State.Wheels) Wheel.SlipAngleRad = Angle;
			const auto Sound = BuildParameters(State, 0.01f, 0.1f);
			TestEqual(TEXT("ordinary curves never excite the squeal layer"), Sound.TireSquealAmplitude, 0.0f);
			TestEqual(TEXT("ordinary curves never add a tire hiss layer"), Sound.TireAmplitude, 0.0f);
		}
	}
	for (const float Speed : {-25.0f, 25.0f})
	{
		auto State = MakeAudioState(Speed, 2800.0f, true);
		for (auto& Wheel : State.Wheels)
		{
			Wheel.LongitudinalSlip = 2.0f;
			Wheel.AngularSpeedRad *= 3.0f;
		}
		TestEqual(TEXT("straight acceleration or wheelspin is not a cornering skid"),
			BuildParameters(State, 0.01f, 0.1f).TireSquealAmplitude, 0.0f);
		for (auto& Wheel : State.Wheels) Wheel.SlipAngleRad = -0.30f;
		TestTrue(TEXT("genuine strong lateral scrub works in either travel direction"),
			BuildParameters(State, 0.01f, 0.1f).TireSquealAmplitude > 0.20f);
	}
	auto Slow = MakeAudioState(7.0f, 1500.0f, true);
	for (auto& Wheel : Slow.Wheels) Wheel.SlipAngleRad = 0.6f;
	TestEqual(TEXT("slow tight turns do not squeal"), BuildParameters(Slow, 0.01f, 0.1f).TireSquealAmplitude, 0.0f);
	return true;
}

#endif
