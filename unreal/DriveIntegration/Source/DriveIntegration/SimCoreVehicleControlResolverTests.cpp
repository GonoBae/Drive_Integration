#include "Misc/AutomationTest.h"
#include "SimCoreVehicleControlResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVehicleControlResolverTest,
	"DriveIntegration.Control.VehicleDirectionResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleControlResolverTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	using namespace SimCoreVehicleControlResolver;

	auto ResolvePedals = [](const float Forward, const float Reverse,
		const EVehicleGear Gear, const bool bFresh, const float Speed)
	{
		FInput Input;
		Input.ForwardPedal = Forward;
		Input.ReversePedal = Reverse;
		Input.Steering = -0.35f;
		Input.bSideBrake = true;
		Input.SelectedGear = Gear;
		Input.bHasFreshAuthoritativeState = bFresh;
		Input.AuthoritativeLongitudinalSpeedMps = Speed;
		Input.DirectionChangeSpeedMps = 0.20f;
		return Resolve(Input);
	};

	const FOutput ReverseAtBoundary = ResolvePedals(
		0.0f, 0.7f, EVehicleGear::Drive, true, 0.20f);
	TestTrue(TEXT("S selects Reverse at the positive speed boundary"),
		ReverseAtBoundary.Gear == EVehicleGear::Reverse);
	TestTrue(TEXT("S applies throttle after selecting Reverse"),
		FMath::IsNearlyEqual(ReverseAtBoundary.Throttle, 0.7f));
	TestTrue(TEXT("Side brake passes through direction selection"),
		ReverseAtBoundary.bSideBrake);
	TestTrue(TEXT("Steering passes through direction selection"),
		FMath::IsNearlyEqual(ReverseAtBoundary.Steering, -0.35f));

	const FOutput BrakeAboveBoundary = ResolvePedals(
		0.0f, 0.7f, EVehicleGear::Drive, true, 0.2001f);
	TestTrue(TEXT("S keeps Drive above the direction-change boundary"),
		BrakeAboveBoundary.Gear == EVehicleGear::Drive);
	TestTrue(TEXT("S is service brake while still moving forward"),
		FMath::IsNearlyEqual(BrakeAboveBoundary.Brake, 0.7f)
			&& FMath::IsNearlyZero(BrakeAboveBoundary.Throttle));

	const FOutput DriveAtBoundary = ResolvePedals(
		0.6f, 0.0f, EVehicleGear::Reverse, true, -0.20f);
	TestTrue(TEXT("W selects Drive at the negative speed boundary"),
		DriveAtBoundary.Gear == EVehicleGear::Drive);
	TestTrue(TEXT("W applies throttle after selecting Drive"),
		FMath::IsNearlyEqual(DriveAtBoundary.Throttle, 0.6f));

	const FOutput BrakeBelowBoundary = ResolvePedals(
		0.6f, 0.0f, EVehicleGear::Reverse, true, -0.2001f);
	TestTrue(TEXT("W keeps Reverse below the direction-change boundary"),
		BrakeBelowBoundary.Gear == EVehicleGear::Reverse);
	TestTrue(TEXT("W is service brake while still reversing"),
		FMath::IsNearlyEqual(BrakeBelowBoundary.Brake, 0.6f)
			&& FMath::IsNearlyZero(BrakeBelowBoundary.Throttle));

	const FOutput StaleSameDirection = ResolvePedals(
		0.5f, 0.0f, EVehicleGear::Drive, false, -5.0f);
	TestTrue(TEXT("Stale state does not suppress same-gear throttle"),
		FMath::IsNearlyEqual(StaleSameDirection.Throttle, 0.5f));
	const FOutput StaleGearChange = ResolvePedals(
		0.0f, 0.5f, EVehicleGear::Drive, false, 0.0f);
	TestTrue(TEXT("Stale state cannot select another gear"),
		StaleGearChange.Gear == EVehicleGear::Drive);
	TestTrue(TEXT("Stale opposite pedal remains service brake"),
		FMath::IsNearlyEqual(StaleGearChange.Brake, 0.5f)
			&& FMath::IsNearlyZero(StaleGearChange.Throttle));

	const FOutput Conflicting = ResolvePedals(
		0.4f, 0.8f, EVehicleGear::Drive, true, 0.0f);
	TestTrue(TEXT("Conflicting pedals apply only the larger service brake"),
		FMath::IsNearlyZero(Conflicting.Throttle)
			&& FMath::IsNearlyEqual(Conflicting.Brake, 0.8f));
	TestTrue(TEXT("Conflicting pedals do not change gear"),
		Conflicting.Gear == EVehicleGear::Drive);

	return true;
}

#endif
