#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreDamagePresentation.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreTurnSignals.h"

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
	FAccumulator Saturated;
	State = {}; State.PlaySessionId = TEXT("saturated");
	State.DamagePercent = 100.f; State.DamageZone = EVehicleDamageZone::Front; State.CollisionEventSequence = 1;
	Advance(Saturated, State);
	State.DamageZone = EVehicleDamageZone::Left; State.CollisionEventSequence = 2;
	State.LastImpactImpulseNs = 7500.f;
	bOk &= TestTrue(TEXT("a new impact on a saturated vehicle can dent a different panel"), Advance(Saturated, State)
		&& Saturated.Weights.Front == 1.f && Saturated.Weights.Left > .2f);
	bOk &= TestFalse(TEXT("saturated damage duplicate cannot deepen a dent"), Advance(Saturated, State));
	FAccumulator Growing;
	State.DamagePercent = 3.f; State.CollisionEventSequence = 1; State.DamageZone = EVehicleDamageZone::Rear;
	Advance(Growing, State);
	State.DamagePercent = 7.f;
	bOk &= TestTrue(TEXT("growing damage within one runtime contact episode refreshes the panel"), Advance(Growing, State)
		&& FMath::IsNearlyEqual(Growing.Weights.Rear, .2f));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreDentGeometryTest,
	"DriveIntegration.Presentation.VehicleDamage.ActualVertexDisplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreDentGeometryTest::RunTest(const FString& Parameters)
{
	SimCoreDamagePresentation::FZoneWeights Weights;
	Weights.Front = 1.0f;
	const FVector Front(201,0,5), Rear(-228,0,5);
	bool Ok = TestTrue(TEXT("front collision changes front silhouette by over 20 cm"),
		USimCoreDeformableBody::DeformVertex(Front, Weights).X < Front.X - 20.0);
	Ok &= TestTrue(TEXT("front hit does not move rear panel"),
		USimCoreDeformableBody::DeformVertex(Rear, Weights).Equals(Rear));
	Weights.Front = 1.f / 35.f;
	Ok &= TestTrue(TEXT("one percent damage is visible, not submillimetre shader displacement"),
		FVector::Distance(Front, USimCoreDeformableBody::DeformVertex(Front, Weights)) > 3.0);
	Weights = {1,1,1,1,1,1};
	for (int32 X=-230; X<=205; X+=15) for (int32 Y=-100; Y<=100; Y+=20) for (int32 Z=-30; Z<=95; Z+=15)
	{
		const FVector Rest(X,Y,Z), Deformed = USimCoreDeformableBody::DeformVertex(Rest, Weights);
		if (Deformed.ContainsNaN() || FVector::Distance(Rest,Deformed) > 28.001)
			return TestTrue(TEXT("all six dents stay finite and bounded"), false);
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreContactLocalDentTest,
	"DriveIntegration.Presentation.VehicleDamage.ContactLocalShapeAndHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreContactLocalDentTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreDamagePresentation;
	SimCoreProtocol::FVehicleState State;
	State.PlaySessionId = TEXT("local-dent"); State.CollisionEventSequence = 1;
	State.DamagePercent = 35; State.DamageZone = SimCoreProtocol::EVehicleDamageZone::Right;
	State.DentPatches.Add({FVector2D(.6,-1),FVector2D(0,1),.5f,.2f});
	FAccumulator Accumulator;
	Advance(Accumulator,State);
	const FVector FrontDoor(121.8,90,8), RearDoor(-138,90,8), OtherSide(121.8,-90,8);
	const auto Deform = [&](FVector Point) { return USimCoreDeformableBody::DeformVertex(Point,Accumulator.Weights); };
	bool Ok = TestTrue(TEXT("actual contact dents only the front door"),
		Deform(FrontDoor).Y < FrontDoor.Y-19.0 && Deform(RearDoor).Equals(RearDoor)
		&& Deform(OtherSide).Equals(OtherSide));
	const FVector FirstDent = Deform(FrontDoor);
	State.DentPatches.Add({FVector2D(-.6,-1),FVector2D(0,1),.5f,.12f});
	State.CollisionEventSequence = 2;
	Advance(Accumulator,State);
	Ok &= TestTrue(TEXT("later rear contact cannot relocate or enlarge the earlier dent"),
		Deform(FrontDoor).Equals(FirstDent) && Deform(RearDoor).Y < RearDoor.Y-11.0);
	Ok &= TestFalse(TEXT("duplicate full-history snapshot changes nothing"),Advance(Accumulator,State));
	FAccumulator Reconnected; Advance(Reconnected,State);
	Ok &= TestTrue(TEXT("late observer reconstructs all contact dents"),
		USimCoreDeformableBody::DeformVertex(FrontDoor,Reconnected.Weights).Equals(FirstDent));
	State = {}; State.PlaySessionId = TEXT("reset"); Advance(Accumulator,State);
	Ok &= TestTrue(TEXT("reset restores the original skin"),Deform(FrontDoor).Equals(FrontDoor));
	State.DamagePercent = 90; State.DamageZone = SimCoreProtocol::EVehicleDamageZone::Right;
	Advance(Accumulator,State);
	Ok &= TestTrue(TEXT("a coarse zone without a measured contact cannot crush an entire side"),Deform(FrontDoor).Equals(FrontDoor));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTurnSignalTest,
	"DriveIntegration.Presentation.TurnSignals.DirectionAndCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreTurnSignalTest::RunTest(const FString& Parameters)
{
	using SimCoreProtocol::ETurnIndicator;
	bool Ok = TestTrue(TEXT("left signal lights only left front/rear/repeater"),
		USimCoreTurnSignals::IsLit(ETurnIndicator::Left,true,0.1,false)
		&& !USimCoreTurnSignals::IsLit(ETurnIndicator::Left,false,0.1,false));
	Ok &= TestFalse(TEXT("off half cycle"), USimCoreTurnSignals::IsLit(ETurnIndicator::Right,false,0.6,false));
	Ok &= TestTrue(TEXT("hazard lights both sides"), USimCoreTurnSignals::IsLit(ETurnIndicator::Off,true,0.9,true)
		&& USimCoreTurnSignals::IsLit(ETurnIndicator::Off,false,0.9,true));
	return Ok;
}

#endif
