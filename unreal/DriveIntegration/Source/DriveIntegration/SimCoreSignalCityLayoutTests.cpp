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
		Layout.Boxes.Num() >= 180 && Layout.Boxes.Num() < 1100);
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
	bOk &= TestTrue(TEXT("substantial ground support"), GroundCount > 100 && GroundCount < 400);
	bOk &= TestTrue(TEXT("curbs/buildings provide bounded static collision"),
		StaticCount > 40 && StaticCount < 200);
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
	const FLayout Layout = SimCoreSignalCity::BuildLayout();
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

	const int32 CurrentMisordered = CountMisorderedSegments(SimCoreSignalCity::BuildLayout());
	const int32 V2Misordered = CountMisorderedSegments(
		SimCoreSignalCity::BuildNaturalLaneMarkingsV2Layout());
	bool bOk = TestEqual(TEXT("current collector tapers preserve left-center-right order"),
		CurrentMisordered, 0);
	bOk &= TestTrue(TEXT("the exact v2 migration source reproduces the crossed taper regression"),
		V2Misordered > 0);
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

#endif
