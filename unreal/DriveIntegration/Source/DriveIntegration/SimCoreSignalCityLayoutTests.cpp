#include "SimCoreSignalCityLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	using SimCoreVirtualCity::EPalette;
	using SimCoreVirtualCity::FLayout;

	FVector EnuToUnrealMeters(const FVector& Enu)
	{
		return FVector(Enu.Y, Enu.X, Enu.Z);
	}

	FVector ForwardEnu(double HeadingDegrees)
	{
		const double Heading = FMath::DegreesToRadians(HeadingDegrees);
		return FVector(FMath::Sin(Heading), FMath::Cos(Heading), 0.0);
	}

	FVector RightEnu(double HeadingDegrees)
	{
		const FVector Forward = ForwardEnu(HeadingDegrees);
		return FVector(Forward.Y, -Forward.X, 0.0);
	}

	bool QueryRoadTop(const FLayout& Layout, const FVector& Point, double& OutTop)
	{
		bool bFound = false;
		OutTop = -DBL_MAX;
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (!Box.bGround || Box.Palette != EPalette::Road) { continue; }
			const FQuat Rotation = FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0.0).Quaternion();
			const FVector Center = EnuToUnrealMeters(Box.CenterEnuM);
			const FVector Normal = Rotation.RotateVector(FVector::UpVector);
			if (Normal.Z <= 0.0) { continue; }
			const double Top = Center.Z + (Box.SizeM.Z * 0.5
				- Normal.X * (Point.Y - Center.X) - Normal.Y * (Point.X - Center.Y)) / Normal.Z;
			const FVector Local = Rotation.UnrotateVector(FVector(Point.Y, Point.X, Top) - Center);
			if (FMath::Abs(Local.X) <= Box.SizeM.X * 0.5 + 1.0e-6
				&& FMath::Abs(Local.Y) <= Box.SizeM.Y * 0.5 + 1.0e-6)
			{
				OutTop = FMath::Max(OutTop, Top);
				bFound = true;
			}
		}
		return bFound;
	}

	bool QueryHighestGroundPalette(const FLayout& Layout, const FVector& Point,
		EPalette& OutPalette)
	{
		bool bFound = false;
		double Highest = -DBL_MAX;
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (!Box.bGround) { continue; }
			const FQuat Rotation = FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0.0).Quaternion();
			const FVector Center = EnuToUnrealMeters(Box.CenterEnuM);
			const FVector Normal = Rotation.RotateVector(FVector::UpVector);
			if (Normal.Z <= 0.0) { continue; }
			const double Top = Center.Z + (Box.SizeM.Z * 0.5
				- Normal.X * (Point.Y - Center.X) - Normal.Y * (Point.X - Center.Y)) / Normal.Z;
			const FVector Local = Rotation.UnrotateVector(FVector(Point.Y, Point.X, Top) - Center);
			if (FMath::Abs(Local.X) <= Box.SizeM.X * 0.5 + 1.0e-6
				&& FMath::Abs(Local.Y) <= Box.SizeM.Y * 0.5 + 1.0e-6
				&& (!bFound || Top > Highest))
			{
				Highest = Top;
				OutPalette = Box.Palette;
				bFound = true;
			}
		}
		return bFound;
	}

	bool StaticFootprintContains(const SimCoreVirtualCity::FBox& Box, const FVector& Point, double Margin)
	{
		const FQuat Rotation = FRotator(0.0, Box.HeadingDegrees, 0.0).Quaternion();
		const FVector Local = Rotation.UnrotateVector(EnuToUnrealMeters(Point - Box.CenterEnuM));
		return FMath::Abs(Local.X) < Box.SizeM.X * 0.5 + Margin
			&& FMath::Abs(Local.Y) < Box.SizeM.Y * 0.5 + Margin;
	}

	bool IdContains(const SimCoreVirtualCity::FBox& Box, const TCHAR* Fragment)
	{
		return Box.Id.ToString().Contains(Fragment, ESearchCase::CaseSensitive);
	}

	const SimCoreVirtualCity::FBox* FindBox(const FLayout& Layout, const FString& Id)
	{
		return Layout.Boxes.FindByPredicate(
			[&Id](const SimCoreVirtualCity::FBox& Box)
			{
				return Box.Id.ToString() == Id;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCityGeometryContractTest,
	"DriveIntegration.SignalCity.GeometryContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCityGeometryContractTest::RunTest(const FString& Parameters)
{
	const FLayout Layout = SimCoreSignalCity::BuildLayout();
	bool bOk = true;
	bOk &= TestEqual(TEXT("versioned map identity"), FString(SimCoreSignalCity::MapId),
		FString(TEXT("signal_city_v2")));
	bOk &= TestTrue(TEXT("bounded code-generated scene"),
		Layout.Boxes.Num() >= 180 && Layout.Boxes.Num() < 3000);
	bOk &= TestTrue(TEXT("bounded dense QA route"),
		Layout.DriveRoute.Num() >= 250 && Layout.DriveRoute.Num() < 700);
	bOk &= TestTrue(TEXT("240m east x 200m north authoring bounds"),
		Layout.GroundHalfExtentNorthEastM.Equals(FVector2D(100.0, 120.0), 1.0e-8));

	TSet<FName> Ids;
	int32 GroundCount = 0;
	int32 StaticCount = 0;
	int32 BarrierCount = 0;
	int32 BackdropCount = 0;
	for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
	{
		const FString Id = Box.Id.ToString();
		bOk &= TestFalse(FString::Printf(TEXT("%s has a deterministic unique ID"), *Id),
			Box.Id.IsNone() || Ids.Contains(Box.Id));
		Ids.Add(Box.Id);
		bOk &= TestTrue(FString::Printf(TEXT("%s has finite positive geometry"), *Id),
			!Box.CenterEnuM.ContainsNaN() && !Box.SizeM.ContainsNaN()
			&& FMath::IsFinite(Box.HeadingDegrees) && FMath::IsFinite(Box.PitchDegrees)
			&& Box.SizeM.X > 0.0 && Box.SizeM.Y > 0.0 && Box.SizeM.Z > 0.0);

		const double Heading = FMath::DegreesToRadians(Box.HeadingDegrees);
		const double EastExtent = FMath::Abs(FMath::Sin(Heading)) * Box.SizeM.X * 0.5
			+ FMath::Abs(FMath::Cos(Heading)) * Box.SizeM.Y * 0.5;
		const double NorthExtent = FMath::Abs(FMath::Cos(Heading)) * Box.SizeM.X * 0.5
			+ FMath::Abs(FMath::Sin(Heading)) * Box.SizeM.Y * 0.5;
		if (Id.StartsWith(TEXT("Backdrop_")))
		{
			++BackdropCount;
			bOk &= TestTrue(FString::Printf(TEXT("%s is visual-only"), *Id),
				!Box.bGround && !Box.bStaticCollider);
		}
		else
		{
			bOk &= TestTrue(FString::Printf(TEXT("%s remains inside east bounds"), *Id),
				FMath::Abs(Box.CenterEnuM.X) + EastExtent <= 120.01);
			bOk &= TestTrue(FString::Printf(TEXT("%s remains inside north bounds"), *Id),
				Box.CenterEnuM.Y - NorthExtent >= -20.01
				&& Box.CenterEnuM.Y + NorthExtent <= 180.01);
		}
		GroundCount += Box.bGround ? 1 : 0;
		StaticCount += Box.bStaticCollider ? 1 : 0;
		BarrierCount += Box.Palette == EPalette::Barrier ? 1 : 0;
	}
	bOk &= TestTrue(TEXT("bounded dense curved ground support"), GroundCount > 100 && GroundCount < 1400);
	bOk &= TestTrue(TEXT("curbs/buildings provide bounded static collision"),
		StaticCount > 40 && StaticCount < 600);
	bOk &= TestEqual(TEXT("no rectangular perimeter barrier"), BarrierCount, 0);
	bOk &= TestTrue(TEXT("visual-only horizon and skyline hide the finite collision bake"),
		BackdropCount >= 19 && FindBox(Layout, TEXT("Backdrop_HorizonGround")) != nullptr);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCityTopologyAndMarkingsTest,
	"DriveIntegration.SignalCity.IntersectionsAndMarkings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCityTopologyAndMarkingsTest::RunTest(const FString& Parameters)
{
	const FLayout Layout = SimCoreSignalCity::BuildLayout();
	int32 IntersectionCount = 0;
	int32 StopLineCount = 0;
	int32 CrosswalkStripeCount = 0;
	int32 ArrowStemCount = 0;
	int32 NonCardinalRoadCount = 0;
	for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
	{
		IntersectionCount += IdContains(Box, TEXT("Road_Intersection_")) ? 1 : 0;
		StopLineCount += IdContains(Box, TEXT("_StopLine")) ? 1 : 0;
		CrosswalkStripeCount += IdContains(Box, TEXT("_Crosswalk_Stripe_")) ? 1 : 0;
		ArrowStemCount += IdContains(Box, TEXT("_Arrow_Stem")) ? 1 : 0;
		if (Box.Palette == EPalette::Road)
		{
			const double Normalized = FMath::Fmod(Box.HeadingDegrees + 360.0, 90.0);
			NonCardinalRoadCount += !FMath::IsNearlyZero(Normalized, 0.5) ? 1 : 0;
		}
	}
	bool bOk = TestEqual(TEXT("two authored four-way intersection plates"), IntersectionCount, 2);
	bOk &= TestEqual(TEXT("one stop line on every approach"), StopLineCount, 8);
	bOk &= TestEqual(TEXT("one six-stripe crosswalk on every approach"), CrosswalkStripeCount, 48);
	bOk &= TestEqual(TEXT("left straight right arrows on every approach"), ArrowStemCount, 24);
	bOk &= TestTrue(TEXT("diagonal and asymmetric curved collectors prevent a rectangular topology"),
		NonCardinalRoadCount >= 12);
	EPalette MergeSurface = EPalette::Ground;
	bOk &= TestTrue(TEXT("saved-raycast regression point has ground"),
		QueryHighestGroundPalette(Layout, FVector(-104.429, 45.623, 0.0), MergeSurface));
	bOk &= TestTrue(TEXT("west collector mouth exposes asphalt above rough sidewalk"),
		MergeSurface == EPalette::Road);

	for (const FVector Intersection : {FVector(0.0, 40.0, 0.0), FVector(0.0, 120.0, 0.0)})
	{
		double Top = 0.0;
		bOk &= TestTrue(TEXT("intersection center has asphalt support"),
			QueryRoadTop(Layout, Intersection, Top));
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (Box.bStaticCollider)
			{
				bOk &= TestFalse(FString::Printf(TEXT("intersection clearance excludes %s"),
					*Box.Id.ToString()), StaticFootprintContains(Box, Intersection, 8.0));
			}
		}
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCityContinuousLaneMarkingsTest,
	"DriveIntegration.SignalCity.ContinuousLaneMarkings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCityContinuousLaneMarkingsTest::RunTest(const FString& Parameters)
{
	const FLayout Layout = SimCoreSignalCity::BuildAlignedCollectorMarkingsV3Layout();
	int32 LinearRoadCount = 0;
	int32 CollectorRoadCount = 0;
	int32 MarkedCollectorChordCount = 0;
	bool bOk = true;
	for (const SimCoreVirtualCity::FBox& Road : Layout.Boxes)
	{
		const FString RoadId = Road.Id.ToString();
		if (Road.Palette != EPalette::Road || !RoadId.EndsWith(TEXT("_Road")))
		{
			continue;
		}
		++LinearRoadCount;
		const bool bCollector = RoadId.StartsWith(TEXT("Collector"));
		CollectorRoadCount += bCollector ? 1 : 0;
		if (bCollector)
		{
			bOk &= TestTrue(FString::Printf(TEXT("%s is an ordinary 10m two-way street"), *RoadId),
				FMath::IsNearlyEqual(Road.SizeM.Y, 10.0, 1.0e-6));
		}
		const FString Prefix = RoadId.LeftChop(5);
		const bool bTaperApproach = Prefix == TEXT("SouthWest")
			|| Prefix == TEXT("SouthEast") || Prefix == TEXT("NorthWest")
			|| Prefix == TEXT("NorthEast");
		const SimCoreVirtualCity::FBox* Centerline = FindBox(Layout, Prefix + TEXT("_Centerline"));
		const SimCoreVirtualCity::FBox* LeftEdge = FindBox(Layout, Prefix + TEXT("_EdgeLine_L"));
		const SimCoreVirtualCity::FBox* RightEdge = FindBox(Layout, Prefix + TEXT("_EdgeLine_R"));
		const bool bTransitionChord = bCollector
			&& ((!Centerline && !LeftEdge && !RightEdge));
		if (bTransitionChord) { continue; }
		bOk &= TestNotNull(FString::Printf(TEXT("%s has a center separator"), *RoadId), Centerline);
		bOk &= TestNotNull(FString::Printf(TEXT("%s has a left edge boundary"), *RoadId), LeftEdge);
		bOk &= TestNotNull(FString::Printf(TEXT("%s has a right edge boundary"), *RoadId), RightEdge);
		if (!Centerline || !LeftEdge || !RightEdge) { continue; }
		MarkedCollectorChordCount += bCollector ? 1 : 0;
		bOk &= TestTrue(FString::Printf(TEXT("%s center separator is yellow"), *RoadId),
			Centerline->Palette == EPalette::Yellow && !Centerline->bGround);
		for (const SimCoreVirtualCity::FBox* Edge : {LeftEdge, RightEdge})
		{
			bOk &= TestTrue(FString::Printf(TEXT("%s edge boundary is non-colliding white marking"), *RoadId),
				Edge->Palette == EPalette::Marking && !Edge->bGround && !Edge->bStaticCollider);
			bOk &= TestTrue(FString::Printf(TEXT("%s edge boundary has the expected approach inset"), *RoadId),
				bTaperApproach ? Edge->SizeM.X + 11.5 < Road.SizeM.X
					: Edge->SizeM.X + 0.51 >= Road.SizeM.X);
		}
		bOk &= TestTrue(FString::Printf(TEXT("%s center separator has the expected approach inset"), *RoadId),
			bTaperApproach ? Centerline->SizeM.X + 11.5 < Road.SizeM.X
				: Centerline->SizeM.X + 0.51 >= Road.SizeM.X);
		bOk &= TestTrue(FString::Printf(TEXT("%s markings follow its heading"), *RoadId),
			FMath::IsNearlyEqual(Centerline->HeadingDegrees, Road.HeadingDegrees, 1.0e-6)
			&& FMath::IsNearlyEqual(LeftEdge->HeadingDegrees, Road.HeadingDegrees, 1.0e-6)
			&& FMath::IsNearlyEqual(RightEdge->HeadingDegrees, Road.HeadingDegrees, 1.0e-6));
	}
	bOk &= TestEqual(TEXT("all seven straight and 26 curved road chords are covered"),
		LinearRoadCount, 33);
	bOk &= TestEqual(TEXT("both collector curves contribute all 26 chords"),
		CollectorRoadCount, 26);
	bOk &= TestEqual(TEXT("middle collector chords carry paint; six end chords per curve defer to smooth tapers"),
		MarkedCollectorChordCount, 14);

	int32 ThinPaintCount = 0;
	for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
	{
		if (Box.Palette != EPalette::Marking && Box.Palette != EPalette::Yellow) continue;
		++ThinPaintCount;
		bOk &= TestTrue(FString::Printf(TEXT("%s is thin non-colliding road paint"), *Box.Id.ToString()),
			!Box.bGround && !Box.bStaticCollider && Box.SizeM.Z <= 0.0021
			&& Box.CenterEnuM.Z + Box.SizeM.Z * 0.5 <= 0.0041);
	}
	bOk &= TestTrue(TEXT("the complete city still has substantial lane paint"), ThinPaintCount > 300);

	int32 TransitionMarkingCount = 0;
	for (const TCHAR* Transition : {
		TEXT("Road_Collector_WestSouthTransition"),
		TEXT("Road_Collector_EastSouthTransition"),
		TEXT("Road_Collector_WestNorthTransition"),
		TEXT("Road_Collector_EastNorthTransition")})
	{
		const FString Prefix(Transition);
		const SimCoreVirtualCity::FBox* Road = FindBox(Layout, Prefix);
		bOk &= TestNotNull(FString::Printf(TEXT("%s asphalt transition exists"), *Prefix), Road);
		bOk &= TestNull(FString::Printf(TEXT("%s no longer uses a single diagonal center bar"), *Prefix),
			FindBox(Layout, Prefix + TEXT("_Centerline")));
		for (const TCHAR* Suffix : {TEXT("_Centerline"), TEXT("_EdgeLine_L"), TEXT("_EdgeLine_R")})
		{
			const SimCoreVirtualCity::FBox* Previous = nullptr;
			for (int32 Segment = 0; Segment < 6; ++Segment)
			{
				const FString Id = Prefix + Suffix
					+ FString::Printf(TEXT("_%02d"), Segment);
				const SimCoreVirtualCity::FBox* Paint = FindBox(Layout, Id);
				bOk &= TestNotNull(FString::Printf(TEXT("%s is present"), *Id), Paint);
				if (!Paint) continue;
				++TransitionMarkingCount;
				bOk &= TestTrue(FString::Printf(TEXT("%s is a short tangent segment"), *Id),
					Paint->SizeM.X < 8.0 && Paint->SizeM.Z <= 0.0021);
				if (Previous)
				{
					const FVector PreviousEnd = Previous->CenterEnuM
						+ ForwardEnu(Previous->HeadingDegrees) * Previous->SizeM.X * 0.5;
					const FVector CurrentStart = Paint->CenterEnuM
						- ForwardEnu(Paint->HeadingDegrees) * Paint->SizeM.X * 0.5;
					bOk &= TestTrue(FString::Printf(TEXT("%s joins without a visible gap"), *Id),
						FVector::Dist2D(PreviousEnd, CurrentStart) < 0.18);
				}
				Previous = Paint;
			}
		}
	}
	bOk &= TestEqual(TEXT("all four collector tapers have three six-segment smooth boundaries"),
		TransitionMarkingCount, 72);
	bOk &= TestNull(TEXT("main intersection conflict box intentionally has no centerline"),
		FindBox(Layout, TEXT("Road_Intersection_Main_Centerline")));
	bOk &= TestNull(TEXT("auxiliary intersection conflict box intentionally has no centerline"),
		FindBox(Layout, TEXT("Road_Intersection_Auxiliary_Centerline")));

	// Preserve an exact pre-migration source so the guarded commandlet can
	// recognize the currently saved map before creating the new actors.
	const FLayout Prior = SimCoreSignalCity::BuildIncompleteLaneMarkingsLayout();
	bOk &= TestNull(TEXT("prior curved collector has no generated centerline"),
		FindBox(Prior, TEXT("CollectorWestCurve_00_Centerline")));
	bOk &= TestNull(TEXT("prior straight approach has no generated edge boundary"),
		FindBox(Prior, TEXT("SouthWest_EdgeLine_L")));
	bOk &= TestNull(TEXT("prior transition apron has no generated merge boundary"),
		FindBox(Prior, TEXT("Road_Collector_WestSouthTransition_EdgeLine_L")));
	const FLayout Raised = SimCoreSignalCity::BuildRaisedLaneMarkingsLayout();
	bOk &= TestNotNull(TEXT("guarded migration retains the exact old diagonal boundary"),
		FindBox(Raised, TEXT("Road_Collector_WestSouthTransition_EdgeLine_L")));
	bOk &= TestNull(TEXT("the exact old layout contains no new distant backdrop"),
		FindBox(Raised, TEXT("Backdrop_HorizonGround")));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCityCollectorLaneOrderingTest,
	"DriveIntegration.SignalCity.CollectorLaneOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCityCollectorLaneOrderingTest::RunTest(const FString& Parameters)
{
	const TCHAR* Transitions[] = {
		TEXT("Road_Collector_WestSouthTransition"),
		TEXT("Road_Collector_EastSouthTransition"),
		TEXT("Road_Collector_WestNorthTransition"),
		TEXT("Road_Collector_EastNorthTransition")
	};
	auto CountMisorderedSegments = [&Transitions](const FLayout& Layout)
	{
		int32 Misordered = 0;
		for (const TCHAR* Transition : Transitions)
		{
			const FString Prefix(Transition);
			for (int32 Segment = 0; Segment < 6; ++Segment)
			{
				const FString Index = FString::Printf(TEXT("_%02d"), Segment);
				const auto* Center = FindBox(Layout, Prefix + TEXT("_Centerline") + Index);
				const auto* Left = FindBox(Layout, Prefix + TEXT("_EdgeLine_L") + Index);
				const auto* Right = FindBox(Layout, Prefix + TEXT("_EdgeLine_R") + Index);
				if (!Center || !Left || !Right)
				{
					++Misordered;
					continue;
				}
				const FVector LocalRight = RightEnu(Center->HeadingDegrees);
				const double LeftOffset = FVector::DotProduct(
					Left->CenterEnuM - Center->CenterEnuM, LocalRight);
				const double RightOffset = FVector::DotProduct(
					Right->CenterEnuM - Center->CenterEnuM, LocalRight);
				// All three cubic polylines use the same stations.  Their lateral
				// order must never swap, otherwise the painted boundaries cross.
				if (LeftOffset >= -0.25 || RightOffset <= 0.25
					|| FVector::Dist2D(Left->CenterEnuM, Right->CenterEnuM) < 1.0)
				{
					++Misordered;
				}
			}
		}
		return Misordered;
	};

	const int32 CurrentMisordered = CountMisorderedSegments(SimCoreSignalCity::BuildAlignedCollectorMarkingsV3Layout());
	const int32 V2Misordered = CountMisorderedSegments(
		SimCoreSignalCity::BuildNaturalLaneMarkingsV2Layout());
	bool bOk = TestEqual(TEXT("v3 migration source preserves left-center-right order"),
		CurrentMisordered, 0);
	bOk &= TestTrue(TEXT("the exact v2 migration source reproduces the crossed taper regression"),
		V2Misordered > 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCitySharedCollectorSurfaceTest,
	"DriveIntegration.SignalCity.SharedCollectorSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCitySharedCollectorSurfaceTest::RunTest(const FString& Parameters)
{
	const FLayout Layout = SimCoreSignalCity::BuildLayout();
	bool bOk = true;
	for (const bool bEast : {false, true})
	{
		const TArray<FVector> Center = SimCoreSignalCity::BuildCollectorCenterline(bEast);
		const FString Prefix = bEast ? TEXT("CollectorEastCurve") : TEXT("CollectorWestCurve");
		bOk &= TestTrue(TEXT("collector contains a dense, bounded shared curve"),
			Center.Num() > 80 && Center.Num() < 160);
		bOk &= TestTrue(TEXT("south collector junction is exact, not a rounded sine/cosine endpoint"),
			Center[0].Equals(FVector(bEast ? 93.0 : -93.0, 40.0, 0.0), 0.0));
		bOk &= TestTrue(TEXT("north collector junction is exact, not a rounded sine/cosine endpoint"),
			Center.Last().Equals(FVector(bEast ? 88.0 : -78.0, 120.0, 0.0), 0.0));
		constexpr double MaximumRoadEnvelopeM = 10.0 + 0.30 + 2.40;
		for (int32 Index = 2; Index < Center.Num(); ++Index)
		{
			const FVector A = Center[Index - 1] - Center[Index - 2];
			const FVector B = Center[Index] - Center[Index - 1];
			const double TwiceArea = FMath::Abs(A.X * B.Y - A.Y * B.X);
			if (TwiceArea <= 1.e-9) continue;
			const double Radius = A.Size2D() * B.Size2D() * (A + B).Size2D() / (2.0 * TwiceArea);
			bOk &= TestTrue(TEXT("turn radius exceeds road half-width plus curb and sidewalk"),
				Radius > MaximumRoadEnvelopeM);
		}
		for (double Side : {-1.0, 1.0})
		{
			const auto Outside = SimCoreSignalCity::OffsetCollectorCenterline(Center, Side * MaximumRoadEnvelopeM);
			for (int32 Index = 1; Index < Outside.Num(); ++Index)
			{
				bOk &= TestTrue(TEXT("even the widest sidewalk offset never folds backwards"),
					FVector::DotProduct(Outside[Index] - Outside[Index - 1],
						(Center[Index] - Center[Index - 1]).GetSafeNormal2D()) > 0.01);
			}
		}
		for (int32 Index = 1; Index < Center.Num(); ++Index)
		{
			const FString Id = Prefix + FString::Printf(TEXT("_%03d"), Index - 1);
			const auto* Road = FindBox(Layout, Id + TEXT("_Road"));
			const auto* Paint = FindBox(Layout, Id + TEXT("_Centerline"));
			const auto* Left = FindBox(Layout, Id + TEXT("_EdgeLine_L"));
			const auto* Right = FindBox(Layout, Id + TEXT("_EdgeLine_R"));
			if (!TestNotNull(TEXT("curve asphalt exists"), Road)
				|| !TestNotNull(TEXT("matching center paint exists"), Paint)
				|| !TestNotNull(TEXT("matching left paint exists"), Left)
				|| !TestNotNull(TEXT("matching right paint exists"), Right)) return false;
			const FVector Midpoint = (Center[Index - 1] + Center[Index]) * 0.5;
			const FVector Travel = (Center[Index] - Center[Index - 1]).GetSafeNormal2D();
			const FVector Across(Travel.Y, -Travel.X, 0);
			bOk &= TestTrue(TEXT("asphalt and paint share the sampled curve, not a corner-cutting straight slab"),
				FVector::Dist2D(Road->CenterEnuM, Midpoint) < 1.e-7
				&& FVector::Dist2D(Paint->CenterEnuM, Midpoint) < 1.e-7
				&& FVector::DotProduct(ForwardEnu(Road->HeadingDegrees), Travel) > 0.999999
				&& Road->HeadingDegrees == Paint->HeadingDegrees && Road->SizeM.X < 2.0);
			bOk &= TestTrue(TEXT("curve boundaries retain left-right order"),
				FVector::DotProduct(Left->CenterEnuM - Midpoint, Across) < -3.5
				&& FVector::DotProduct(Right->CenterEnuM - Midpoint, Across) > 3.5);
			for (double Side : {-3.6, 0.0, 3.6})
			{
				EPalette Palette = EPalette::Ground;
				bOk &= TestTrue(TEXT("both complete traffic footprints expose asphalt, not curb/sidewalk"),
					QueryHighestGroundPalette(Layout, Midpoint + Across * Side, Palette)
					&& Palette == EPalette::Road);
			}
		}
	}
	for (bool bEast : {false, true})
	{
		const TArray<FVector> Center = SimCoreSignalCity::BuildCollectorCenterline(bEast);
		for (double Offset : {-2.0, 2.0})
		{
			const TArray<FVector> Lane = SimCoreSignalCity::OffsetCollectorCenterline(Center, Offset);
			for (int32 Index = 1; Index < Lane.Num(); ++Index)
			{
				const FVector Travel = (Lane[Index] - Lane[Index - 1]).GetSafeNormal2D();
				const FVector Across(Travel.Y, -Travel.X, 0);
				const int32 Samples = FMath::CeilToInt(FVector::Dist2D(Lane[Index - 1], Lane[Index]));
				for (int32 Sample = 0; Sample <= Samples; ++Sample)
				{
					for (double Side : {-1.5, 0.0, 1.5})
					{
						const FVector Test = FMath::Lerp(Lane[Index - 1], Lane[Index],
							static_cast<double>(Sample) / Samples) + Across * Side;
						EPalette Palette = EPalette::Ground;
						bOk &= TestTrue(TEXT("complete lane width remains supported at every curve joint and midpoint"),
							QueryHighestGroundPalette(Layout, Test, Palette) && Palette == EPalette::Road);
					}
				}
			}
		}
	}
	const FVector OuterJoint(104.960285171, 119.671259612, 0.0);
	double Top = 0.0;
	bOk &= TestFalse(TEXT("exact v4 source reproduces the outside-joint asphalt gap"),
		QueryRoadTop(SimCoreSignalCity::BuildUnsealedCollectorCurvesV4Layout(), OuterJoint, Top));
	bOk &= TestTrue(TEXT("miter overlap closes the outside-joint gap without changing lane geometry"),
		QueryRoadTop(SimCoreSignalCity::BuildSealedCollectorCurvesV5Layout(), OuterJoint, Top)
		&& FMath::Abs(Top) < 1.e-7);
	for (const TCHAR* Id : {TEXT("Road_Collector_WestSouthTransition"),
		TEXT("Road_Collector_EastSouthTransition"), TEXT("Road_Collector_WestNorthTransition"),
		TEXT("Road_Collector_EastNorthTransition")})
	{
		bOk &= TestNull(TEXT("obsolete square asphalt apron is removed"), FindBox(Layout, Id));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCityDriveRouteTest,
	"DriveIntegration.SignalCity.NonRectangularDriveRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCityDriveRouteTest::RunTest(const FString& Parameters)
{
	const FLayout Layout = SimCoreSignalCity::BuildLayout();
	if (!TestTrue(TEXT("route exists"), Layout.DriveRoute.Num() >= 250)) { return false; }
	bool bOk = TestTrue(TEXT("route closes exactly"),
		Layout.DriveRoute[0].PositionEnuM.Equals(Layout.DriveRoute.Last().PositionEnuM, 1.0e-7));
	double Length = 0.0;
	bool bSawMainIntersection = false;
	bool bSawAuxiliaryIntersection = false;
	bool bSawDiagonalHeading = false;
	for (int32 Index = 0; Index < Layout.DriveRoute.Num(); ++Index)
	{
		const auto& Checkpoint = Layout.DriveRoute[Index];
		double Top = 0.0;
		bOk &= TestTrue(FString::Printf(TEXT("route checkpoint %d has asphalt support"), Index),
			QueryRoadTop(Layout, Checkpoint.PositionEnuM, Top));
		bOk &= TestTrue(TEXT("route follows the asphalt top"),
			FMath::Abs(Top - Checkpoint.PositionEnuM.Z) < 0.01);
		bSawMainIntersection |= FVector::Dist2D(Checkpoint.PositionEnuM, FVector(0.0, 40.0, 0.0)) < 2.0;
		bSawAuxiliaryIntersection |= FVector::Dist2D(Checkpoint.PositionEnuM, FVector(0.0, 120.0, 0.0)) < 2.0;
		const double CardinalRemainder = FMath::Fmod(Checkpoint.HeadingDegrees + 360.0, 90.0);
		bSawDiagonalHeading |= !FMath::IsNearlyZero(CardinalRemainder, 2.0);
		if (Index > 0)
		{
			const double Step = FVector::Dist(Layout.DriveRoute[Index - 1].PositionEnuM,
				Checkpoint.PositionEnuM);
			bOk &= TestTrue(TEXT("route sampling is positive and no more than 1.5m"),
				Step > 0.0 && Step <= 1.51);
			Length += Step;
		}
	}
	bOk &= TestTrue(TEXT("short-city lap length is bounded"), Length > 430.0 && Length < 650.0);
	bOk &= TestTrue(TEXT("route traverses the central four-way intersection"), bSawMainIntersection);
	bOk &= TestTrue(TEXT("route traverses the offset diagonal intersection"), bSawAuxiliaryIntersection);
	bOk &= TestTrue(TEXT("route contains genuine non-cardinal travel"), bSawDiagonalHeading);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalCityCollectorLaneMergeTest,
	"DriveIntegration.SignalCity.ContinuousCollectorLaneMerges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSignalCityCollectorLaneMergeTest::RunTest(const FString& Parameters)
{
	const FLayout Layout = SimCoreSignalCity::BuildLayout();
	const FLayout Prior = SimCoreSignalCity::BuildUnmergedCollectorLanesV6Layout();
	bool bOk = true;
	for (const auto& Old : Prior.Boxes)
	{
		const auto* Current = FindBox(Layout, Old.Id.ToString());
		bOk &= TestTrue(TEXT("lane-merge migration preserves every existing road, collider and marking"),
			Current && Current->CenterEnuM.Equals(Old.CenterEnuM, 0.0)
			&& Current->SizeM.Equals(Old.SizeM, 0.0) && Current->HeadingDegrees == Old.HeadingDegrees
			&& Current->bGround == Old.bGround && Current->bStaticCollider == Old.bStaticCollider);
	}
	TArray<TArray<FVector>> Merges;
	for (bool bEast : {false, true})
	{
		const FString Collector = bEast ? TEXT("CollectorEastCurve") : TEXT("CollectorWestCurve");
		const auto Center = SimCoreSignalCity::BuildCollectorCenterline(bEast);
		const FVector Incoming(bEast ? 1.0 : -1.0, 0, 0);
		const FVector Right(Incoming.Y, -Incoming.X, 0);
		for (bool bSouth : {true, false})
		{
			const FVector Junction = bSouth ? Center[0] : Center.Last();
			for (int32 Side : {-1, 1})
			{
				for (int32 Boundary : {1, 2})
				{
					const FString Prefix = Collector + (bSouth ? TEXT("_MergeSouth") : TEXT("_MergeNorth"))
						+ (Side < 0 ? TEXT("_L") : TEXT("_R")) + FString::Printf(TEXT("_%d"), Boundary);
					TArray<FVector> Points;
					for (int32 Segment = 0; Segment < 64; ++Segment)
					{
						const auto* Paint = FindBox(Layout, Prefix + FString::Printf(TEXT("_%02d"), Segment));
						if (!Paint) break;
						const FVector HalfSegment = ForwardEnu(Paint->HeadingDegrees) * (Paint->SizeM.X - 0.08) * 0.5;
						const FVector Start = Paint->CenterEnuM - HalfSegment;
						const FVector End = Paint->CenterEnuM + HalfSegment;
						bOk &= TestTrue(TEXT("merge paint has constant width and no collision or height obstacle"),
							Paint->Palette == EPalette::Marking && !Paint->bGround && !Paint->bStaticCollider
							&& FMath::IsNearlyEqual(Paint->SizeM.Y, 0.14, 1.e-7) && Paint->SizeM.Z <= 0.0021);
						if (Points.IsEmpty()) Points.Add(Start);
						else bOk &= TestTrue(TEXT("successive merge segments meet without disappearing"),
							FVector::Dist2D(Points.Last(), Start) < 1.e-7);
						Points.Add(End);
						for (const FVector Sample : {Start, (Start + End) * 0.5, End})
						{
							double Top = 0;
							bOk &= TestTrue(TEXT("entire merge marking stays on the unchanged asphalt"),
								QueryRoadTop(Layout, Sample, Top) && FMath::Abs(Top) < 1.e-7);
						}
					}
					if (!TestTrue(TEXT("each approach divider has a visible staged merge"), Points.Num() > 10)) return false;
					const FVector Divider = Junction + Right * Side * (0.05 + 3.3 * Boundary);
					bOk &= TestTrue(TEXT("merge starts on the existing straight divider and crosses the same junction point"),
						FVector::Dist2D(Points[0], Divider - Incoming * 6.0) < 1.e-7
						&& FVector::Dist2D(Points[1], Divider) < 1.e-7);
					double DistanceToEdge = DBL_MAX;
					for (const auto& Edge : Layout.Boxes)
					{
						const FString Id = Edge.Id.ToString();
						if (!Id.StartsWith(Collector) || !Id.Contains(TEXT("_EdgeLine_"))) continue;
						const FVector Half = ForwardEnu(Edge.HeadingDegrees) * (Edge.SizeM.X - 0.08) * 0.5;
						DistanceToEdge = FMath::Min(DistanceToEdge, FVector::Dist2D(Points.Last(),
							FMath::ClosestPointOnSegment(Points.Last(), Edge.CenterEnuM - Half, Edge.CenterEnuM + Half)));
					}
					bOk &= TestTrue(TEXT("divider terminates on the existing outer boundary, never in open asphalt"), DistanceToEdge < 1.e-7);
					Merges.Add(MoveTemp(Points));
				}
			}
		}
	}
	auto Cross = [](const FVector& A, const FVector& B) { return A.X * B.Y - A.Y * B.X; };
	for (int32 First = 0; First < Merges.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < Merges.Num(); ++Second)
		{
			for (int32 A = 1; A < Merges[First].Num(); ++A)
			{
				for (int32 B = 1; B < Merges[Second].Num(); ++B)
				{
					const FVector P = Merges[First][A - 1], Q = Merges[First][A];
					const FVector R = Merges[Second][B - 1], S = Merges[Second][B];
					bOk &= TestFalse(TEXT("staged merge dividers never cross one another"),
						Cross(Q - P, R - P) * Cross(Q - P, S - P) < -1.e-9
						&& Cross(S - R, P - R) * Cross(S - R, Q - R) < -1.e-9);
				}
			}
		}
	}
	bOk &= TestEqual(TEXT("four joins each connect two lane dividers on both road sides"), Merges.Num(), 16);
	return bOk;
}

#endif
