#include "SimCoreSignalCityTrafficLayout.h"
#include "SimCoreSignalCityLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

namespace
{
	const SimCoreSignalCity::FTrafficLane* FindLane(
		const SimCoreSignalCity::FTrafficLayout& Layout, uint32 Id)
	{
		return Layout.Lanes.FindByPredicate([Id](const auto& Lane) { return Lane.Id == Id; });
	}

	bool IsClosedRoute(const SimCoreSignalCity::FTrafficLayout& Layout,
		std::initializer_list<uint32> Ids)
	{
		if (Ids.size() < 2) { return false; }
		TArray<uint32> Route;
		for (uint32 Id : Ids) { Route.Add(Id); }
		for (int32 Index = 0; Index < Route.Num(); ++Index)
		{
			const auto* Lane = FindLane(Layout, Route[Index]);
			const auto* Next = FindLane(Layout, Route[(Index+1) % Route.Num()]);
			if (!Lane || !Next || !Lane->Successors.Contains(Next->Id)
				|| !Lane->PointsEnuM.Last().Equals(Next->PointsEnuM[0], 0.15))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSignalCityTrafficTopologyTest,
	"DriveIntegration.SignalCity.Traffic.TwoControllerTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSignalCityTrafficTopologyTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSignalCity;
	const FTrafficLayout Layout = BuildTrafficLayout();
	FString Error;
	bool bOk = TestTrue(TEXT("bounded topology and asphalt support"),
		ValidateTrafficLayout(Layout, Error));
	if (!bOk) { AddError(Error); return false; }
	bOk &= TestEqual(TEXT("two independent controllers"), Layout.SignalPlans.Num(), 2);
	bOk &= TestEqual(TEXT("base topology plus dedicated three-lane approaches"), Layout.Lanes.Num(), 58);
	bOk &= TestEqual(TEXT("vehicle and pedestrian heads at both intersections"), Layout.Signals.Num(), 16);
	int32 PedestrianHeads = 0;
	for (const auto& Signal : Layout.Signals)
	{
		PedestrianHeads += Signal.Kind == ETrafficSignalKind::Pedestrian ? 1 : 0;
	}
	bOk &= TestEqual(TEXT("eight pedestrian signal heads"), PedestrianHeads, 8);
	bOk &= TestNotEqual(TEXT("controller offset is deliberately coordinated, not cloned"),
		Layout.SignalPlans[0].OffsetMs, Layout.SignalPlans[1].OffsetMs);

	int32 ControlledApproaches = 0;
	TSet<uint32> Groups;
	for (const FTrafficLane& Lane : Layout.Lanes)
	{
		if (!Lane.SignalGroupId) { continue; }
		++ControlledApproaches;
		Groups.Add(Lane.SignalGroupId);
		bOk &= TestEqual(TEXT("each marked lane has exactly its dedicated movement"),
			Lane.Successors.Num(), 1);
	}
	bOk &= TestEqual(TEXT("all four approaches at both intersections are controlled"),
		ControlledApproaches, 24);
	bOk &= TestTrue(TEXT("groups are globally distinct per controller"),
		Groups.Num() == 8 && Groups.Contains(101) && Groups.Contains(102) && Groups.Contains(103) && Groups.Contains(104)
		&& Groups.Contains(201) && Groups.Contains(202) && Groups.Contains(203) && Groups.Contains(204));
	bOk &= TestTrue(TEXT("clockwise collector loop crosses both signalized streets"),
		IsClosedRoute(Layout, {1010,1011,1014,3001,2020,2021,2024,3004}));
	bOk &= TestTrue(TEXT("counter-clockwise collector loop crosses both signalized streets"),
		IsClosedRoute(Layout, {1020,1021,1024,3002,2010,2011,2014,3003}));
	bOk &= TestTrue(TEXT("northbound central route is a real through road"),
		FindLane(Layout, 1030) && FindLane(Layout, 1034) && FindLane(Layout, 2034));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSignalCityTrafficRejectionTest,
	"DriveIntegration.SignalCity.Traffic.RejectUnsafePlans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSignalCityTrafficRejectionTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSignalCity;
	const FTrafficLayout Source = BuildTrafficLayout();
	FString Error;
	bool bOk = true;
	FTrafficLayout Changed = Source;
	Changed.SignalPlans[1].Groups[0] = 101;
	bOk &= TestFalse(TEXT("one group cannot be owned by two controllers"),
		ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.SignalPlans[0].Phases[1].YellowGroups = {101};
	bOk &= TestFalse(TEXT("one group cannot be green and yellow together"),
		ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.SignalPlans[1].OffsetMs = 72000;
	bOk &= TestFalse(TEXT("offset must remain inside its cycle"),
		ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Signals[4].ControllerId = 1;
	bOk &= TestFalse(TEXT("head controller must own its group"),
		ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].Successors = {999};
	bOk &= TestFalse(TEXT("dangling movement rejected"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].LaneChanges[0].TargetLaneId = 1020;
	bOk &= TestFalse(TEXT("opposite approach is not a passing lane"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].LaneChanges[0].SourceBeginM = 0;
	bOk &= TestFalse(TEXT("change window cannot start inside collector merge"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].LaneChanges.Reset();
	bOk &= TestFalse(TEXT("one-sided lane-change permission is rejected"), ValidateTrafficLayout(Changed, Error));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSignalCityTrafficJsonTest,
	"DriveIntegration.SignalCity.Traffic.DeterministicV2Json",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSignalCityTrafficJsonTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSignalCity;
	FTrafficLayout Layout = BuildTrafficLayout();
	FString First, Second, Error;
	const FString Checksum = TEXT("fnv1a64:0123456789abcdef");
	bool bOk = TestTrue(TEXT("canonical v2 serialization succeeds"),
		SerializeTrafficLayout(Layout, Checksum, First, Error));
	Algo::Reverse(Layout.Lanes);
	Algo::Reverse(Layout.Signals);
	Algo::Reverse(Layout.SignalPlans);
	for (auto& Lane : Layout.Lanes) { Algo::Reverse(Lane.Successors); Algo::Reverse(Lane.LaneChanges); }
	for (auto& Plan : Layout.SignalPlans)
	{
		Algo::Reverse(Plan.Groups);
		for (auto& Phase : Plan.Phases)
		{
			Algo::Reverse(Phase.GreenGroups);
			Algo::Reverse(Phase.YellowGroups);
		}
	}
	bOk &= TestTrue(TEXT("reordered unordered collections serialize"),
		SerializeTrafficLayout(Layout, Checksum, Second, Error));
	bOk &= TestEqual(TEXT("insertion order cannot change package bytes"), First, Second);
	bOk &= TestTrue(TEXT("schema and controller provenance are explicit"),
		First.Contains(TEXT("\"format_version\":2"))
		&& First.Contains(TEXT("\"controller_id\":2"))
		&& First.Contains(TEXT("\"kind\":\"pedestrian\""))
		&& First.Contains(TEXT("\"lane_changes\""))
		&& First.Contains(TEXT("\"target_lane_id\":1015"))
		&& First.Contains(TEXT("\"signal_plans\"")));
	bOk &= TestTrue(TEXT("LF terminated without CR"),
		First.EndsWith(TEXT("\n")) && !First.Contains(TEXT("\r")));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSignalCityPassingApproachesTest,
	"DriveIntegration.SignalCity.Traffic.SameDirectionPassingApproaches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSignalCityPassingApproachesTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSignalCity;
	const FTrafficLayout Layout = BuildTrafficLayout();
	bool bOk = true;
	for (uint32 SourceId : {1010U, 1020U, 1030U, 1040U, 2010U, 2020U, 1034U, 2040U})
	{
		const FTrafficLane* Source = FindLane(Layout, SourceId);
		const FTrafficLane* Neighbor = FindLane(Layout, SourceId == 1034 ? 2035 : SourceId + 5);
		const FTrafficLane* Right = FindLane(Layout, SourceId == 1034 ? 2036 : SourceId + 6);
		if (!TestNotNull(TEXT("original approach exists"), Source)
			|| !TestNotNull(TEXT("parallel approach exists"), Neighbor)
			|| !TestNotNull(TEXT("dedicated right-turn lane exists"), Right)) { return false; }
		bOk &= TestEqual(TEXT("each dedicated city lane is 3.2m wide"), Source->WidthM, 3.2);
		bOk &= TestTrue(TEXT("collector entry is shared but three stop positions remain separate"),
			Source->PointsEnuM[0].Equals(Neighbor->PointsEnuM[0], 1.e-6)
			&& FMath::IsNearlyEqual(FVector::Dist2D(Source->PointsEnuM.Last(), Neighbor->PointsEnuM.Last()), 3.3, 1.e-6)
			&& FMath::IsNearlyEqual(FVector::Dist2D(Source->PointsEnuM.Last(), Right->PointsEnuM.Last()), 3.3, 1.e-6));
		bOk &= TestEqual(TEXT("centre lane can change to both adjacent lanes"), Source->LaneChanges.Num(), 2);
		const FTrafficLaneChange& Change = Source->LaneChanges[0];
		bOk &= TestTrue(TEXT("change corridor excludes entry fan and stopline approach"),
			Change.SourceBeginM >= 14.0 && Change.TargetBeginM >= 14.0
			&& Change.SourceEndM - Change.SourceBeginM >= 8.0);
		bOk &= TestEqual(TEXT("neighbor uses same stopline signal"), Source->SignalGroupId, Neighbor->SignalGroupId);
		const int32 Mid = Source->PointsEnuM.Num() / 2;
		bOk &= TestTrue(TEXT("actual centers, not just painted arrows, are 3.3m apart"),
			FMath::IsNearlyEqual(FVector::Dist2D(Source->PointsEnuM[Mid], Neighbor->PointsEnuM[Mid]), 3.3, 1.e-6));
		bOk &= TestTrue(TEXT("left/straight/right lanes lead to different real connectors"),
			Source->Successors[0] != Neighbor->Successors[0] && Source->Successors[0] != Right->Successors[0]
			&& Neighbor->Successors[0] != Right->Successors[0]);
	}
	for (const FSignalPlan& Plan : Layout.SignalPlans)
	{
		for (const FSignalPhase& Phase : Plan.Phases)
		{
			int32 VehicleGreen = 0, PedestrianGreen = 0;
			for (const FTrafficSignal& Signal : Layout.Signals)
			{
				if (Signal.ControllerId != Plan.Id || !Phase.GreenGroups.Contains(Signal.GroupId)) continue;
				Signal.Kind == ETrafficSignalKind::Vehicle ? ++VehicleGreen : ++PedestrianGreen;
			}
			bOk &= TestTrue(TEXT("protected approach never conflicts with opposing turns or WALK"),
				VehicleGreen <= 1 && !(VehicleGreen > 0 && PedestrianGreen > 0));
		}
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSignalCitySidewalkWaitingTest,
	"DriveIntegration.SignalCity.Traffic.SidewalkWaitingAndProtectedCrossings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSignalCitySidewalkWaitingTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSignalCity;
	const FTrafficLayout Traffic = BuildTrafficLayout();
	const auto Ground = BuildLayout();
	const auto Previous = BuildInitialThreeLaneLayout();
	const auto SafeCrossings = BuildIncompleteLaneMarkingsLayout();
	bool bOk = true;
	int32 WaitingPositions = 0;
	for (const FTrafficLane& Lane : Traffic.Lanes)
	{
		if (!Lane.SignalGroupId) continue;
		const FVector Junction(0.0, Lane.SignalGroupId >= 200 ? 120.0 : 40.0, 0.0);
		const FVector Forward = (Lane.PointsEnuM.Last() - Lane.PointsEnuM[Lane.PointsEnuM.Num()-2]).GetSafeNormal2D();
		bOk &= TestTrue(TEXT("every controlled endpoint stops before the 15m outer zebra"),
			FMath::IsNearlyEqual(FVector::DotProduct(Junction - Lane.PointsEnuM.Last(), Forward), 18.0, 1.e-6));
	}
	// Marking migration must not change the collision map or silently accept a
	// different prior generation when updating an already widened saved level.
	for (const auto& Box : SafeCrossings.Boxes)
	{
		if (!Box.bGround && !Box.bStaticCollider) continue;
		const auto* Old = Previous.Boxes.FindByPredicate([&](const auto& Value) { return Value.Id == Box.Id; });
		bOk &= TestTrue(TEXT("safe-crossing migration preserves ground/collider geometry"),
			Old && Box.CenterEnuM.Equals(Old->CenterEnuM, 1.e-8)
			&& Box.SizeM.Equals(Old->SizeM, 1.e-8) && Box.HeadingDegrees == Old->HeadingDegrees);
	}
	for (const FTrafficSignal& Head : Traffic.Signals)
	{
		if (Head.Kind != ETrafficSignalKind::Pedestrian) continue;
		const auto* Other = Traffic.Signals.FindByPredicate([&](const auto& Value)
		{
			return Value.Kind == ETrafficSignalKind::Pedestrian && Value.Id != Head.Id
				&& Value.ControllerId == Head.ControllerId && Value.GroupId == Head.GroupId;
		});
		if (!TestNotNull(TEXT("paired pedestrian head exists"), Other)) return false;
		const FVector Along = (Other->PositionEnuM - Head.PositionEnuM).GetSafeNormal2D();
		const FVector Side(-Along.Y, Along.X, 0.0);
		const double CrossingSeconds = FVector::Dist2D(Head.PositionEnuM, Other->PositionEnuM) / 1.35;
		const auto* Plan = Traffic.SignalPlans.FindByPredicate([&](const auto& Value) { return Value.Id == Head.ControllerId; });
		bOk &= TestTrue(TEXT("18-second exclusive WALK accommodates a complete crossing plus margin"),
			Plan && Plan->Phases.ContainsByPredicate([&](const auto& Phase)
			{
				return Phase.GreenGroups.Contains(Head.GroupId)
					&& Phase.DurationMs / 1000.0 >= CrossingSeconds + 0.2;
			}));
		for (double Offset : {-0.45, 0.45})
		{
			++WaitingPositions;
			const FVector Wait = Head.PositionEnuM + Side * Offset;
			bool bEntireCapsuleOnSidewalk = false;
			for (const auto& Box : Ground.Boxes)
			{
				if (!Box.bGround || Box.Palette != SimCoreVirtualCity::EPalette::Sidewalk) continue;
				const double Angle = FMath::DegreesToRadians(Box.HeadingDegrees);
				const FVector Forward(FMath::Sin(Angle), FMath::Cos(Angle), 0.0);
				const FVector Right(FMath::Cos(Angle), -FMath::Sin(Angle), 0.0);
				const FVector Delta = Wait - Box.CenterEnuM;
				bEntireCapsuleOnSidewalk |= FMath::Abs(FVector::DotProduct(Delta, Forward)) + 0.35 <= Box.SizeM.X * 0.5
					&& FMath::Abs(FVector::DotProduct(Delta, Right)) + 0.35 <= Box.SizeM.Y * 0.5;
			}
			bOk &= TestTrue(FString::Printf(TEXT("head %u offset %.2f: full standing capsule rests on sidewalk"), Head.Id, Offset),
				bEntireCapsuleOnSidewalk);
			double MinimumClearance = DBL_MAX;
			uint32 ClosestLane = 0;
			for (const FTrafficLane& Lane : Traffic.Lanes)
			{
				for (int32 Segment = 1; Segment < Lane.PointsEnuM.Num(); ++Segment)
				{
					const FVector A = Lane.PointsEnuM[Segment-1], B = Lane.PointsEnuM[Segment];
					const FVector Forward = (B-A).GetSafeNormal2D();
					const FVector Right(Forward.Y, -Forward.X, 0.0);
					const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A,B) / 0.20));
					for (int32 Step = 0; Step <= Steps; ++Step)
					{
						const FVector Delta = Wait - FMath::Lerp(A, B, static_cast<double>(Step) / Steps);
						const double LongGap = FMath::Max(0.0, FMath::Abs(FVector::DotProduct(Delta, Forward)) - 2.2);
						const double SideGap = FMath::Max(0.0, FMath::Abs(FVector::DotProduct(Delta, Right)) - 1.0);
						const double Clearance = FMath::Sqrt(LongGap*LongGap + SideGap*SideGap) - 0.35;
						if (Clearance < MinimumClearance) { MinimumClearance = Clearance; ClosestLane = Lane.Id; }
					}
				}
			}
			bOk &= TestTrue(FString::Printf(TEXT("head %u offset %.2f: NPC footprint clear (lane %u, %.3fm)"),
				Head.Id, Offset, ClosestLane, MinimumClearance), MinimumClearance > 0.05);
		}
	}
	bOk &= TestEqual(TEXT("both walking offsets at every endpoint, including return-trip waiting"), WaitingPositions, 16);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreSignalCityCollectorTangentsTest,
	"DriveIntegration.SignalCity.Traffic.CollectorTangentContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSignalCityCollectorTangentsTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSignalCity;
	const FTrafficLayout Traffic = BuildTrafficLayout();
	FString Error;
	bool bOk = TestTrue(TEXT("smoothed collector center and both lane edges remain on authored asphalt"),
		ValidateTrafficLayout(Traffic, Error));
	if (!bOk) { AddError(Error); return false; }
	const auto AngleDegrees = [](const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(A.GetSafeNormal2D(), B.GetSafeNormal2D()), -1.0, 1.0)));
	};
	for (uint32 Id : {3001U, 3002U, 3003U, 3004U})
	{
		const auto* Lane = FindLane(Traffic, Id);
		if (!TestNotNull(TEXT("collector exists"), Lane)) return false;
		TArray<FVector> Shared = OffsetCollectorCenterline(
			BuildCollectorCenterline(Id == 3001 || Id == 3003), Id >= 3003 ? -2.0 : 2.0);
		if (Id >= 3003) { Algo::Reverse(Shared); }
		bOk &= TestEqual(TEXT("NPC route has every shared asphalt curve sample"),
			Lane->PointsEnuM.Num(), Shared.Num());
		for (int32 Index = 0; Index < FMath::Min(Lane->PointsEnuM.Num(), Shared.Num()); ++Index)
		{
			bOk &= TestTrue(TEXT("NPC route follows the road's offset curve, without a separate shortcut"),
				Lane->PointsEnuM[Index].Equals(Shared[Index], 1.e-7));
		}
		const FVector Incoming(Id == 3001 || Id == 3003 ? 1.0 : -1.0, 0, 0);
		const FVector Outgoing = -Incoming;
		const FVector First = Lane->PointsEnuM[1] - Lane->PointsEnuM[0];
		const FVector Last = Lane->PointsEnuM.Last() - Lane->PointsEnuM[Lane->PointsEnuM.Num()-2];
		bOk &= TestTrue(FString::Printf(TEXT("collector %u sampled entry agrees with incoming road"), Id),
			AngleDegrees(Incoming, First) < 3.0);
		bOk &= TestTrue(FString::Printf(TEXT("collector %u sampled exit agrees with outgoing road"), Id),
			AngleDegrees(Last, Outgoing) < 3.0);
		double MaximumStep = 0.0;
		for (int32 Index = 2; Index < Lane->PointsEnuM.Num(); ++Index)
		{
			MaximumStep = FMath::Max(MaximumStep, AngleDegrees(
				Lane->PointsEnuM[Index-1] - Lane->PointsEnuM[Index-2],
				Lane->PointsEnuM[Index] - Lane->PointsEnuM[Index-1]));
		}
		bOk &= TestTrue(FString::Printf(TEXT("collector %u has no backwards/cusp heading step (%.3fdeg)"), Id, MaximumStep),
			MaximumStep < 8.0);
		for (uint32 NextId : Lane->Successors)
		{
			const auto* Next = FindLane(Traffic, NextId);
			if (!TestNotNull(TEXT("collector joins a real dedicated approach"), Next)) return false;
			bOk &= TestTrue(TEXT("all three destination fans preserve the exact collector join"),
				Lane->PointsEnuM.Last().Equals(Next->PointsEnuM[0], 1.e-7));
			bOk &= TestTrue(TEXT("sampled merge agrees with each smooth entry fan"),
				AngleDegrees(Last, Next->PointsEnuM[1] - Next->PointsEnuM[0]) < 6.0);
		}
	}
	return bOk;
}
#endif
