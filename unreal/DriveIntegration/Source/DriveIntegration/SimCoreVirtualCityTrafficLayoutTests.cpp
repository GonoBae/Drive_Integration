#include "SimCoreVirtualCityTrafficLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"

namespace
{
	const SimCoreVirtualCity::FTrafficLane* FindLane(const SimCoreVirtualCity::FTrafficLayout& Layout, uint32 Id)
	{
		return Layout.Lanes.FindByPredicate([Id](const auto& Lane) { return Lane.Id == Id; });
	}

	double LoopSignedArea(const SimCoreVirtualCity::FTrafficLayout& Layout, uint32 Start)
	{
		double Area = 0;
		uint32 Id = Start;
		TSet<uint32> Seen;
		while (!Seen.Contains(Id))
		{
			const auto* Lane = FindLane(Layout, Id);
			if (!Lane || Lane->Successors.IsEmpty()) { return 0; }
			Seen.Add(Id);
			for (int32 Index = 1; Index < Lane->PointsEnuM.Num(); ++Index)
			{
				const FVector& A = Lane->PointsEnuM[Index-1];
				const FVector& B = Lane->PointsEnuM[Index];
				Area += A.X*B.Y - B.X*A.Y;
			}
			// The lowest-ID successor is the through movement, never the bay spur.
			Id = Lane->Successors[0];
		}
		return Id == Start && Seen.Num() == 10 ? Area : 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVirtualCityTrafficTopologyTest,
	"DriveIntegration.VirtualCity.Traffic.DirectionalTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityTrafficTopologyTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	const FTrafficLayout Layout = BuildTrafficLayout();
	FString Error;
	if (!TestTrue(TEXT("bounded topology and full-width road support: ") + Error, ValidateTrafficLayout(Layout, Error)))
	{
		AddError(Error); return false;
	}
	bool bOk = TestEqual(TEXT("separate approaches/connectors/segments"), Layout.Lanes.Num(), 25);
	bOk &= TestEqual(TEXT("three physical heads, two phase groups"), Layout.Signals.Num(), 3);
	bOk &= TestTrue(TEXT("inner right-hand loop clockwise"), LoopSignedArea(Layout, 100) < -40000);
	bOk &= TestTrue(TEXT("outer right-hand loop counterclockwise"), LoopSignedArea(Layout, 200) > 40000);
	const FTrafficLane* East = FindLane(Layout, 100);
	const FTrafficLane* West = FindLane(Layout, 200);
	const FTrafficLane* Branch = FindLane(Layout, 310);
	const FTrafficLane* Terminal = FindLane(Layout, 340);
	if (!East || !West || !Branch || !Terminal) { return false; }
	bOk &= TestTrue(TEXT("eastbound approach stops before intersection"), East->PointsEnuM.Last().Equals(FVector(-8,140,0), 0.01));
	bOk &= TestTrue(TEXT("westbound approach stops before intersection"), West->PointsEnuM.Last().Equals(FVector(8,144,0), 0.01));
	bOk &= TestTrue(TEXT("branch stops at existing authored marking"), Branch->PointsEnuM.Last().Equals(FVector(-2,152,0), 0.01));
	bOk &= TestEqual(TEXT("eastbound phase"), East->SignalGroupId, 1u);
	bOk &= TestEqual(TEXT("westbound phase"), West->SignalGroupId, 1u);
	bOk &= TestEqual(TEXT("branch phase"), Branch->SignalGroupId, 2u);
	bOk &= TestTrue(TEXT("main eastbound has no conflicting left turn"), East->Successors == TArray<uint32>{110});
	bOk &= TestTrue(TEXT("main westbound straight and conflict-free right only"), West->Successors == TArray<uint32>({210,300}));
	bOk &= TestTrue(TEXT("branch has explicit right and left connectors"), Branch->Successors == TArray<uint32>({320,330}));
	bOk &= TestTrue(TEXT("bay has an explicit terminal and no forced U-turn"), Terminal->bTerminal && Terminal->Successors.IsEmpty());
	int32 TerminalCount = 0;
	for (const FTrafficLane& Lane : Layout.Lanes)
	{
		TerminalCount += Lane.bTerminal ? 1 : 0;
		bOk &= TestFalse(TEXT("branch incoming source cannot be reached by a bay U-turn"), Lane.Successors.Contains(310));
		if (Lane.Id == 300 || Lane.Id == 320 || Lane.Id == 330)
		{
			bOk &= TestEqual(TEXT("stop is on approach, not after the connector"), Lane.SignalGroupId, 0u);
		}
	}
	bOk &= TestEqual(TEXT("only outgoing branch terminates"), TerminalCount, 1);
	const auto* Right = FindLane(Layout, 300);
	if (!Right) { return false; }
	for (int32 Index = 1; Index < Right->PointsEnuM.Num(); ++Index)
	{
		const FVector& A = Right->PointsEnuM[Index-1];
		const FVector& B = Right->PointsEnuM[Index];
		const FVector Normal = FVector(B.Y-A.Y, A.X-B.X, 0).GetSafeNormal();
		bOk &= TestTrue(TEXT("group-1 right-turn corridor does not cross eastbound corridor"),
			FMath::Min(A.Y, B.Y) - FMath::Abs(Normal.Y) * Right->WidthM * 0.5 > 141.5);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVirtualCityTrafficRejectionTest,
	"DriveIntegration.VirtualCity.Traffic.RejectInvalidTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityTrafficRejectionTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	const FTrafficLayout Source = BuildTrafficLayout();
	FString Error;
	bool bOk = true;
	FTrafficLayout Changed = Source;
	Changed.Lanes[0].Successors = {999};
	bOk &= TestFalse(TEXT("dangling successor rejected"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes.Last().bTerminal = false;
	bOk &= TestFalse(TEXT("unmarked dead end rejected"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].Successors = {120};
	bOk &= TestFalse(TEXT("teleport across stop line rejected"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[1].Id = Changed.Lanes[0].Id;
	bOk &= TestFalse(TEXT("duplicate lane ID rejected"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Lanes[0].PointsEnuM[2].Z += 0.5;
	bOk &= TestFalse(TEXT("unsupported lane elevation rejected"), ValidateTrafficLayout(Changed, Error));
	Changed = Source;
	Changed.Signals[2].PositionEnuM.X += 50;
	bOk &= TestFalse(TEXT("unassociated branch head rejected"), ValidateTrafficLayout(Changed, Error));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreVirtualCityTrafficJsonTest,
	"DriveIntegration.VirtualCity.Traffic.DeterministicJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityTrafficJsonTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	FTrafficLayout Layout = BuildTrafficLayout();
	FString First, Second, Error;
	const FString Checksum = TEXT("fnv1a64:0123456789abcdef");
	bool bOk = TestTrue(TEXT("canonical serialization succeeds"), SerializeTrafficLayout(Layout, Checksum, First, Error));
	Algo::Reverse(Layout.Lanes);
	Algo::Reverse(Layout.Signals);
	for (auto& Lane : Layout.Lanes) { Algo::Reverse(Lane.Successors); }
	bOk &= TestTrue(TEXT("reordered topology serializes"), SerializeTrafficLayout(Layout, Checksum, Second, Error));
	bOk &= TestEqual(TEXT("array insertion order cannot change package bytes"), First, Second);
	bOk &= TestTrue(TEXT("LF terminated without CR"), First.EndsWith(TEXT("\n")) && !First.Contains(TEXT("\r")));
	bOk &= TestTrue(TEXT("source collision checksum is explicit"), First.Contains(TEXT("\"source_map_checksum\":\"fnv1a64:0123456789abcdef\"")));
	bOk &= TestFalse(TEXT("missing checksum cannot be exported"), SerializeTrafficLayout(Layout, TEXT(""), Second, Error));
	bOk &= TestFalse(TEXT("bad hexadecimal checksum cannot be exported"), SerializeTrafficLayout(Layout, TEXT("fnv1a64:zzzzzzzzzzzzzzzz"), Second, Error));
	return bOk;
}
#endif
