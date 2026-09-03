#include "SimCoreVirtualCityLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	FVector EnuToUnrealMeters(const FVector& Enu)
	{
		return FVector(Enu.Y, Enu.X, Enu.Z);
	}

	bool QueryRoadTop(const SimCoreVirtualCity::FLayout& Layout, double East, double North, double& OutTop)
	{
		bool bFound = false;
		OutTop = -DBL_MAX;
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (!Box.bGround || Box.Palette != SimCoreVirtualCity::EPalette::Road)
			{
				continue;
			}
			const FQuat Rotation = FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0.0).Quaternion();
			const FVector Center = EnuToUnrealMeters(Box.CenterEnuM);
			const FVector Normal = Rotation.RotateVector(FVector::UpVector);
			if (Normal.Z <= 0.0)
			{
				continue;
			}
			const double Top = Center.Z + (Box.SizeM.Z * 0.5
				- Normal.X * (North - Center.X) - Normal.Y * (East - Center.Y)) / Normal.Z;
			const FVector Local = Rotation.UnrotateVector(FVector(North, East, Top) - Center);
			if (FMath::Abs(Local.X) <= Box.SizeM.X * 0.5 + 1.0e-6
				&& FMath::Abs(Local.Y) <= Box.SizeM.Y * 0.5 + 1.0e-6)
			{
				bFound = true;
				OutTop = FMath::Max(OutTop, Top);
			}
		}
		return bFound;
	}

	bool IsWithinStaticFootprint(const SimCoreVirtualCity::FBox& Box, const FVector& Point, double Margin = 0.0)
	{
		const FQuat Rotation = FRotator(0.0, Box.HeadingDegrees, 0.0).Quaternion();
		const FVector Local = Rotation.UnrotateVector(EnuToUnrealMeters(Point - Box.CenterEnuM));
		return FMath::Abs(Local.X) < Box.SizeM.X * 0.5 + Margin
			&& FMath::Abs(Local.Y) < Box.SizeM.Y * 0.5 + Margin;
	}

	const SimCoreVirtualCity::FBox* FindBox(
		const SimCoreVirtualCity::FLayout& Layout, const TCHAR* Id)
	{
		return Layout.Boxes.FindByPredicate([Id](const SimCoreVirtualCity::FBox& Box)
		{
			return Box.Id == FName(Id);
		});
	}

	double TopFaceCenterHeight(const SimCoreVirtualCity::FBox& Box)
	{
		const FQuat Rotation = FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0.0).Quaternion();
		const FVector TopNormal = Rotation.RotateVector(FVector::UpVector);
		return Box.CenterEnuM.Z + TopNormal.Z * Box.SizeM.Z * 0.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVirtualCityGeometryTest,
	"DriveIntegration.VirtualCity.GeometryContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityGeometryTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	const FLayout Layout = BuildLayout();
	bool bSuccess = true;
	bSuccess &= TestTrue(TEXT("240m east x 200m north footprint"),
		Layout.GroundHalfExtentNorthEastM.Equals(FVector2D(100.0, 120.0), 1.0e-8));
	bSuccess &= TestTrue(TEXT("ground footprint center is ENU 0,80,0"),
		Layout.GroundCenterEnuM.Equals(FVector(0.0, 80.0, 0.0), 1.0e-8));
	bSuccess &= TestTrue(TEXT("substantial but bounded asset-free blockout"),
		Layout.Boxes.Num() > 200 && Layout.Boxes.Num() < 1000);
	TSet<FName> Ids;
	int32 GroundCount = 0;
	int32 StaticCount = 0;
	int32 BuildingCount = 0;
	int32 GradeCount = 0;
	int32 SidewalkGroundCount = 0;
	int32 CurbGroundCount = 0;
	for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
	{
		const FString Id = Box.Id.ToString();
		bSuccess &= TestFalse(FString::Printf(TEXT("%s id is nonempty"), *Id), Box.Id.IsNone());
		bSuccess &= TestFalse(FString::Printf(TEXT("%s id is unique"), *Id), Ids.Contains(Box.Id));
		Ids.Add(Box.Id);
		bSuccess &= TestTrue(FString::Printf(TEXT("%s transform is finite"), *Id),
			IsFiniteVector(Box.CenterEnuM) && IsFiniteVector(Box.SizeM)
				&& FMath::IsFinite(Box.HeadingDegrees) && FMath::IsFinite(Box.PitchDegrees));
		bSuccess &= TestTrue(FString::Printf(TEXT("%s dimensions are positive"), *Id),
			Box.SizeM.X > 0.0 && Box.SizeM.Y > 0.0 && Box.SizeM.Z > 0.0);
		bSuccess &= TestTrue(FString::Printf(TEXT("%s dual ground/collider role is curb-only"), *Id),
			!Box.bGround || !Box.bStaticCollider || Box.Palette == EPalette::Curb);
		if (Box.bGround)
		{
			++GroundCount;
			bSuccess &= TestTrue(TEXT("only terrain, roads, curb tops and sidewalks are exported as ground"),
				Box.Palette == EPalette::Ground || Box.Palette == EPalette::Road
					|| Box.Palette == EPalette::Curb || Box.Palette == EPalette::Sidewalk);
			SidewalkGroundCount += Box.Palette == EPalette::Sidewalk ? 1 : 0;
			CurbGroundCount += Box.Palette == EPalette::Curb ? 1 : 0;
			if (!FMath::IsNearlyZero(Box.PitchDegrees))
			{
				++GradeCount;
			}
		}
		if (Box.bStaticCollider)
		{
			++StaticCount;
			bSuccess &= TestTrue(FString::Printf(TEXT("%s collider is a supported upright OBB"), *Id),
				FMath::IsNearlyZero(Box.PitchDegrees));
			bSuccess &= TestTrue(FString::Printf(TEXT("%s collision has a visible material category"), *Id),
				Box.Palette == EPalette::Curb || Box.Palette == EPalette::Building || Box.Palette == EPalette::Barrier);
		}
		if (Box.Palette == EPalette::Building)
		{
			++BuildingCount;
			bSuccess &= TestTrue(TEXT("building masses have authoritative collision proxies"), Box.bStaticCollider);
		}
	}
	bSuccess &= TestEqual(TEXT("floor, road, curb-top and sidewalk support component count"), GroundCount, 390);
	bSuccess &= TestEqual(TEXT("every authored sidewalk supplies baked wheel support"),
		SidewalkGroundCount, 116);
	bSuccess &= TestEqual(TEXT("every authored curb supplies a baked tire support top"),
		CurbGroundCount, 215);
	bSuccess &= TestTrue(TEXT("visible curbs and barriers have collision proxies"), StaticCount > 100);
	bSuccess &= TestEqual(TEXT("eight modular building masses"), BuildingCount, 8);
	bSuccess &= TestEqual(TEXT("graded road and both adjacent sidewalk slabs"), GradeCount, 6);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVirtualCityCurbSidewalkTopAlignmentTest,
	"DriveIntegration.VirtualCity.CurbSidewalkTopAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityCurbSidewalkTopAlignmentTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	const FLayout Layout = BuildLayout();
	const SimCoreVirtualCity::FBox* Curb = FindBox(Layout, TEXT("Curb_South_1"));
	const SimCoreVirtualCity::FBox* Sidewalk = FindBox(Layout, TEXT("Sidewalk_South_1"));
	if (!TestNotNull(TEXT("representative south curb"), Curb)
		|| !TestNotNull(TEXT("representative south sidewalk"), Sidewalk))
	{
		return false;
	}

	const double CurbTop = TopFaceCenterHeight(*Curb);
	const double SidewalkTop = TopFaceCenterHeight(*Sidewalk);
	bool bSuccess = true;
	bSuccess &= TestTrue(TEXT("curb top remains 24 cm above its road surface"),
		FMath::IsNearlyEqual(CurbTop, 0.24, 1.0e-6));
	bSuccess &= TestTrue(TEXT("sidewalk wheel-support plane is flush with curb top"),
		FMath::IsNearlyEqual(SidewalkTop, CurbTop, 1.0e-6));
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVirtualCityRouteTest,
	"DriveIntegration.VirtualCity.ClosedDriveRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityRouteTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	const FLayout Layout = BuildLayout();
	if (!TestTrue(TEXT("dense full lap route exists"), Layout.DriveRoute.Num() > 300))
	{
		return false;
	}
	bool bSuccess = true;
	bSuccess &= TestTrue(TEXT("spawn is ENU origin on southern eastbound lane"),
		Layout.DriveRoute[0].PositionEnuM.Equals(FVector::ZeroVector, 1.0e-8));
	bSuccess &= TestEqual(TEXT("spawn faces East"), Layout.DriveRoute[0].HeadingDegrees, 90.0);
	bSuccess &= TestTrue(TEXT("route closes exactly at spawn"),
		Layout.DriveRoute.Last().PositionEnuM.Equals(Layout.DriveRoute[0].PositionEnuM, 1.0e-8));
	bSuccess &= TestEqual(TEXT("closed route preserves heading"), Layout.DriveRoute.Last().HeadingDegrees, 90.0);
	double Length = 0.0;
	double SignedAreaTwice = 0.0;
	for (int32 Index = 0; Index < Layout.DriveRoute.Num(); ++Index)
	{
		const FCheckpoint& Point = Layout.DriveRoute[Index];
		bSuccess &= TestTrue(TEXT("route pose finite"), IsFiniteVector(Point.PositionEnuM) && FMath::IsFinite(Point.HeadingDegrees));
		double SurfaceTop = 0.0;
		const bool bRoad = QueryRoadTop(Layout, Point.PositionEnuM.X, Point.PositionEnuM.Y, SurfaceTop);
		bSuccess &= TestTrue(FString::Printf(TEXT("route point %d is on a road, not only the base floor"), Index), bRoad);
		if (bRoad)
		{
			bSuccess &= TestTrue(FString::Printf(TEXT("route point %d follows slab surface height"), Index),
				FMath::Abs(SurfaceTop - Point.PositionEnuM.Z) < 0.01);
		}
		const double Heading = FMath::DegreesToRadians(Point.HeadingDegrees);
		const FVector Forward(FMath::Sin(Heading), FMath::Cos(Heading), 0.0);
		const FVector Right(FMath::Cos(Heading), -FMath::Sin(Heading), 0.0);
		for (int32 Front = -1; Front <= 1; Front += 2)
		{
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const FVector Wheel = Point.PositionEnuM + Forward * (Front * 1.4) + Right * (Side * 0.85);
				double WheelTop = 0.0;
				bSuccess &= TestTrue(FString::Printf(TEXT("route point %d wheel footprint remains on asphalt"), Index),
					QueryRoadTop(Layout, Wheel.X, Wheel.Y, WheelTop));
			}
		}
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (Box.bStaticCollider && IsWithinStaticFootprint(Box, Point.PositionEnuM, 1.1))
			{
				bSuccess &= TestFalse(FString::Printf(TEXT("route point %d maintains clearance from %s"),
					Index, *Box.Id.ToString()), true);
			}
		}
		if (Index > 0)
		{
			const FVector& Previous = Layout.DriveRoute[Index - 1].PositionEnuM;
			const double Step = FVector::Dist(Previous, Point.PositionEnuM);
			bSuccess &= TestTrue(TEXT("sample step is positive and no more than 2m"), Step > 0.0 && Step <= 2.0);
			Length += Step;
			SignedAreaTwice += Previous.X * Point.PositionEnuM.Y - Point.PositionEnuM.X * Previous.Y;
		}
	}
	bSuccess &= TestTrue(TEXT("loop length fits a short drive rather than a disconnected test pad"), Length > 620.0 && Length < 650.0);
	bSuccess &= TestTrue(TEXT("right-hand lane full lap is counterclockwise in ENU"), SignedAreaTwice > 0.0);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVirtualCityGradeTest,
	"DriveIntegration.VirtualCity.GradeSurfaceAndJunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVirtualCityGradeTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVirtualCity;
	const FLayout Layout = BuildLayout();
	bool bSuccess = true;
	const double Slope = FMath::Tan(FMath::DegreesToRadians(4.0));
	const FVector2D CornerCenters[] = {
		FVector2D(76.0, 18.0), FVector2D(76.0, 126.0),
		FVector2D(-76.0, 126.0), FVector2D(-76.0, 18.0)
	};
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		for (int32 Sample = 0; Sample <= 360; ++Sample)
		{
			const double Angle = FMath::DegreesToRadians(-90.0 + Corner * 90.0 + Sample * 0.25);
			const double East = CornerCenters[Corner].X + 19.5 * FMath::Cos(Angle);
			const double North = CornerCenters[Corner].Y + 19.5 * FMath::Sin(Angle);
			double Top = 0.0;
			bSuccess &= TestTrue(FString::Printf(TEXT("corner %d outer road edge has no slab seam at sample %d"), Corner, Sample),
				QueryRoadTop(Layout, East, North, Top));
			bSuccess &= TestTrue(TEXT("outer corner remains asphalt height, not the lower base floor"), FMath::Abs(Top) < 0.01);
		}
	}
	for (int32 Sample = 0; Sample <= 432; ++Sample)
	{
		const double North = 18.0 + Sample * 0.25;
		double ExpectedTop = 0.0;
		if (North > 40.0 && North < 60.0)
		{
			ExpectedTop = (North - 40.0) * Slope;
		}
		else if (North >= 60.0 && North <= 80.0)
		{
			ExpectedTop = 20.0 * Slope;
		}
		else if (North > 80.0 && North < 100.0)
		{
			ExpectedTop = (100.0 - North) * Slope;
		}
		for (double East : {89.0, 92.0, 95.0})
		{
			double Top = 0.0;
			bSuccess &= TestTrue(TEXT("grade spans both driving lanes without holes"), QueryRoadTop(Layout, East, North, Top));
			bSuccess &= TestTrue(TEXT("inclined slab endpoints follow a 4deg road profile"), FMath::Abs(Top - ExpectedTop) < 0.01);
		}
	}
	for (int32 Sample = 0; Sample <= 28; ++Sample)
	{
		const FVector Point(0.0, 142.0 + Sample, 0.0);
		double Top = 0.0;
		bSuccess &= TestTrue(TEXT("northern T-junction reaches the stopping bay"), QueryRoadTop(Layout, Point.X, Point.Y, Top));
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (Box.bStaticCollider)
			{
				bSuccess &= TestFalse(TEXT("intersection center has no curb or building blocking access"),
					IsWithinStaticFootprint(Box, Point, 1.0));
			}
		}
	}
	return bSuccess;
}

#endif
