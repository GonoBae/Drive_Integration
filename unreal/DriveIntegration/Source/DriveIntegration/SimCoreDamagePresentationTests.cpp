#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreDamagePresentation.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreDamagePresentationTest,
	"DriveIntegration.Presentation.VehicleDamage.LocalDentAccumulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDamagePresentationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	using namespace SimCoreDamagePresentation;
	FAccumulator Accumulator;
	FVehicleState State;
	State.PlaySessionId = TEXT("play-a");

	bool bOk = true;
	bOk &= TestFalse(TEXT("undamaged initial state needs no material refresh"),
		Advance(Accumulator, State));
	bOk &= TestTrue(TEXT("all initial dent weights are zero"),
		Accumulator.Weights.IsNearlyZero());

	State.DamagePercent = 7.0f;
	State.DamageZone = EVehicleDamageZone::Front;
	State.CollisionEventSequence = 1;
	bOk &= TestTrue(TEXT("a new front impact refreshes WPO parameters"),
		Advance(Accumulator, State));
	bOk &= TestTrue(TEXT("front damage maps to a bounded visible weight"),
		FMath::IsNearlyEqual(Accumulator.Weights.Front, 0.2f));

	State.DamagePercent = 14.0f;
	State.DamageZone = EVehicleDamageZone::Right;
	State.CollisionEventSequence = 2;
	bOk &= TestTrue(TEXT("a later right impact refreshes WPO parameters"),
		Advance(Accumulator, State));
	bOk &= TestTrue(TEXT("the earlier front dent remains when another zone is hit"),
		FMath::IsNearlyEqual(Accumulator.Weights.Front, 0.2f));
	bOk &= TestTrue(TEXT("only the new damage delta is assigned to the right"),
		FMath::IsNearlyEqual(Accumulator.Weights.Right, 0.2f));
	bOk &= TestFalse(TEXT("a duplicate snapshot cannot accumulate the same event twice"),
		Advance(Accumulator, State));

	State.DamagePercent = 70.0f;
	State.DamageZone = EVehicleDamageZone::Underbody;
	State.CollisionEventSequence = 3;
	bOk &= TestTrue(TEXT("large impact updates the affected zone"),
		Advance(Accumulator, State));
	bOk &= TestTrue(TEXT("each zone is capped at the material's maximum dent"),
		FMath::IsNearlyEqual(Accumulator.Weights.Underbody, 1.0f));

	State = {};
	State.PlaySessionId = TEXT("play-b");
	bOk &= TestTrue(TEXT("PIE reset clears existing dents"),
		Advance(Accumulator, State));
	bOk &= TestTrue(TEXT("all zone weights are zero after reset"),
		Accumulator.Weights.IsNearlyZero());

	FAccumulator MidSession;
	State.DamagePercent = 70.0f;
	State.DamageZone = EVehicleDamageZone::Rear;
	State.CollisionEventSequence = 5;
	bOk &= TestTrue(TEXT("late observer seeds the last authoritative zone"),
		Advance(MidSession, State));
	bOk &= TestTrue(TEXT("late observer still receives a bounded dent"),
		FMath::IsNearlyEqual(MidSession.Weights.Rear, 1.0f));

	bOk &= TestEqual(TEXT("front parameter name is stable"),
		MaterialParameterName(EVehicleDamageZone::Front), FName(TEXT("DentFront")));
	return bOk;
}

#endif
