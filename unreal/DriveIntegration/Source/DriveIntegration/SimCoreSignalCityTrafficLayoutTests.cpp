#include "SimCoreSignalCityTrafficLayout.h"

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
		bOk &= TestEqual(TEXT("each signalized approach exposes straight/right/left"),
			Lane.Successors.Num(), 3);
	}
	bOk &= TestEqual(TEXT("all four approaches at both intersections are controlled"),
		ControlledApproaches, 8);
	bOk &= TestTrue(TEXT("groups are globally distinct per controller"),
		Groups.Num() == 4 && Groups.Contains(101) && Groups.Contains(102)
		&& Groups.Contains(201) && Groups.Contains(202));
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
	Changed.SignalPlans[1].OffsetMs = 36000;
	bOk &= TestFalse(TEXT("offset must remain inside its cycle"),
		ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Signals[4].ControllerId = 1;
	bOk &= TestFalse(TEXT("head controller must own its group"),
		ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].Successors = {999};
	bOk &= TestFalse(TEXT("dangling movement rejected"), ValidateTrafficLayout(Changed, Error));
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
	for (auto& Lane : Layout.Lanes) { Algo::Reverse(Lane.Successors); }
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
		&& First.Contains(TEXT("\"signal_plans\"")));
	bOk &= TestTrue(TEXT("LF terminated without CR"),
		First.EndsWith(TEXT("\n")) && !First.Contains(TEXT("\r")));
	return bOk;
}
#endif
