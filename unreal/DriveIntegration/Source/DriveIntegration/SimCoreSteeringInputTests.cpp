#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreSteeringInput.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreKeyboardSteeringRampTest,
	"DriveIntegration.Control.KeyboardSteering.FrameIndependentRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreKeyboardSteeringRampTest::RunTest(const FString& Parameters)
{
	const auto Simulate = [](const int32 Hertz, const float Seconds, const bool bLeft)
	{
		float Command = 0.0f;
		const int32 Steps = FMath::RoundToInt(Hertz * Seconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Command = SimCoreSteeringInput::AdvanceKeyboardCommand(
				Command, bLeft, !bLeft, 1.0f / Hertz, 1.25f, 2.0f);
		}
		return Command;
	};

	bool bOk = true;
	const float Left30 = Simulate(30, 0.4f, true);
	const float Left60 = Simulate(60, 0.4f, true);
	const float Left120 = Simulate(120, 0.4f, true);
	bOk &= TestTrue(TEXT("keyboard ramp is frame-rate independent"),
		FMath::IsNearlyEqual(Left30, 0.5f, 1.0e-5f)
		&& FMath::IsNearlyEqual(Left60, Left30, 1.0e-5f)
		&& FMath::IsNearlyEqual(Left120, Left30, 1.0e-5f));
	bOk &= TestTrue(TEXT("left and right ramps are symmetric"),
		FMath::IsNearlyEqual(Simulate(60, 0.4f, false), -Left60, 1.0e-5f));

	float Released = Left60;
	for (int32 Step = 0; Step < 15; ++Step)
	{
		Released = SimCoreSteeringInput::AdvanceKeyboardCommand(
			Released, false, false, 1.0f / 60.0f, 1.25f, 2.0f);
	}
	bOk &= TestTrue(TEXT("released keyboard steering self-centres"),
		FMath::IsNearlyZero(Released, 1.0e-5f));
	bOk &= TestEqual(TEXT("analog steering stays direct when keyboard is idle"),
		SimCoreSteeringInput::ResolveCommand(0.0f, false, -0.37f), -0.37f);
	bOk &= TestEqual(TEXT("keyboard owns steering with fine control near centre"),
		SimCoreSteeringInput::ResolveCommand(0.25f, true, -0.8f), 0.0625f);
	bOk &= TestEqual(TEXT("keyboard shaping is left/right symmetric"),
		SimCoreSteeringInput::ResolveCommand(-0.25f, true, 0.8f), -0.0625f);
	bOk &= TestEqual(TEXT("keyboard full lock remains full lock"),
		SimCoreSteeringInput::ResolveCommand(1.0f, true, 0.0f), 1.0f);
	return bOk;
}

#endif
