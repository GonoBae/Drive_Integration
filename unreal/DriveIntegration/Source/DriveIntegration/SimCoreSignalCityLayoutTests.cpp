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
		Layout.Boxes.Num() >= 180 && Layout.Boxes.Num() < 700);
	bOk &= TestTrue(TEXT("bounded dense QA route"),
		Layout.DriveRoute.Num() >= 250 && Layout.DriveRoute.Num() < 700);
	bOk &= TestTrue(TEXT("240m east x 200m north authoring bounds"),
		Layout.GroundHalfExtentNorthEastM.Equals(FVector2D(100.0, 120.0), 1.0e-8));

	TSet<FName> Ids;
	int32 GroundCount = 0;
	int32 StaticCount = 0;
	int32 BarrierCount = 0;
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
		bOk &= TestTrue(FString::Printf(TEXT("%s remains inside east bounds"), *Id),
			FMath::Abs(Box.CenterEnuM.X) + EastExtent <= 120.01);
		bOk &= TestTrue(FString::Printf(TEXT("%s remains inside north bounds"), *Id),
			Box.CenterEnuM.Y - NorthExtent >= -20.01
			&& Box.CenterEnuM.Y + NorthExtent <= 180.01);
		GroundCount += Box.bGround ? 1 : 0;
		StaticCount += Box.bStaticCollider ? 1 : 0;
		BarrierCount += Box.Palette == EPalette::Barrier ? 1 : 0;
	}
	bOk &= TestTrue(TEXT("substantial ground support"), GroundCount > 100 && GroundCount < 400);
	bOk &= TestTrue(TEXT("curbs/buildings provide bounded static collision"),
		StaticCount > 40 && StaticCount < 200);
	bOk &= TestEqual(TEXT("no rectangular perimeter barrier"), BarrierCount, 0);
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
	bOk &= TestEqual(TEXT("one direction arrow on every approach"), ArrowStemCount, 8);
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
