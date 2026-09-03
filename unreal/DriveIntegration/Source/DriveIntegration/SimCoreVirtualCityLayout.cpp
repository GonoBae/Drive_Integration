#include "SimCoreVirtualCityLayout.h"

namespace SimCoreVirtualCity
{
namespace
{
	constexpr double RoadWidth = 8.0;
	constexpr double RoadThickness = 0.24;
	constexpr double CornerRadius = 16.0;
	constexpr double LaneCornerRadius = 18.0;
	constexpr int32 CornerSegments = 12;
	constexpr double RampDegrees = 4.0;
	constexpr double CurbWidth = 0.30;
	constexpr double CurbHeight = 0.24;
	constexpr double SidewalkWidth = 2.50;

	double RampHeight(double North)
	{
		const double Slope = FMath::Tan(FMath::DegreesToRadians(RampDegrees));
		if (North <= 40.0 || North >= 100.0)
		{
			return 0.0;
		}
		if (North < 60.0)
		{
			return (North - 40.0) * Slope;
		}
		if (North <= 80.0)
		{
			return 20.0 * Slope;
		}
		return (100.0 - North) * Slope;
	}

	FVector ForwardEnu(double HeadingDegrees)
	{
		const double Heading = FMath::DegreesToRadians(HeadingDegrees);
		return FVector(FMath::Sin(Heading), FMath::Cos(Heading), 0.0);
	}

	void AddBox(
		FLayout& Layout, const FString& Id, const FVector& Center,
		const FVector& Size, double Heading, EPalette Palette,
		bool bGround = false, bool bStaticCollider = false, double Pitch = 0.0)
	{
		FBox& Box = Layout.Boxes.AddDefaulted_GetRef();
		Box.Id = FName(*Id);
		Box.CenterEnuM = Center;
		Box.SizeM = Size;
		Box.HeadingDegrees = Heading;
		Box.PitchDegrees = Pitch;
		Box.Palette = Palette;
		Box.bGround = bGround;
		Box.bStaticCollider = bStaticCollider;
	}

	// Place an oriented slab by its top-face midpoint, not its volume center.
	// Otherwise inclined road endpoints move horizontally and leave contact gaps.
	void AddTopSlab(
		FLayout& Layout, const FString& Id, const FVector& TopMidpoint,
		double HorizontalLength, double Width, double Thickness,
		double Heading, double Pitch, EPalette Palette, bool bGround = false)
	{
		const double PitchRadians = FMath::DegreesToRadians(Pitch);
		const FVector TopNormal = -ForwardEnu(Heading) * FMath::Sin(PitchRadians)
			+ FVector::UpVector * FMath::Cos(PitchRadians);
		AddBox(Layout, Id, TopMidpoint - TopNormal * Thickness * 0.5,
			FVector(HorizontalLength / FMath::Cos(PitchRadians), Width, Thickness),
			Heading, Palette, bGround, false, Pitch);
	}

	void AddRoad(
		FLayout& Layout, const FString& Id, const FVector& TopMidpoint,
		double HorizontalLength, double Heading, double Pitch = 0.0,
		double Width = RoadWidth)
	{
		AddTopSlab(Layout, Id, TopMidpoint, HorizontalLength, Width,
			RoadThickness, Heading, Pitch, EPalette::Road, true);
	}

	void AddCurb(
		FLayout& Layout, const FString& Id, const FVector& SurfaceMidpoint,
		double Length, double Heading)
	{
		AddBox(Layout, Id, SurfaceMidpoint + FVector(0.0, 0.0, CurbHeight * 0.5),
			FVector(Length, CurbWidth, CurbHeight), Heading, EPalette::Curb, true, true);
	}

	void AddSidewalk(
		FLayout& Layout, const FString& Id, const FVector& SurfaceMidpoint,
		double Length, double Heading, double Pitch = 0.0)
	{
		// Keep the exported wheel-support plane flush with the visible curb top.
		// A lower sidewalk lets the tire settle behind the curb while its sidewall
		// still overlaps the curb mesh, which looks like the wheel is buried.
		AddTopSlab(Layout, Id, SurfaceMidpoint + FVector(0.0, 0.0, CurbHeight),
			Length, SidewalkWidth, 0.22, Heading, Pitch, EPalette::Sidewalk, true);
	}

	void AddCorner(
		FLayout& Layout, const FString& Prefix, const FVector2D& CenterEnu,
		double StartAngleDegrees)
	{
		const double HalfStep = FMath::DegreesToRadians(90.0 / CornerSegments * 0.5);
		for (int32 Index = 0; Index < CornerSegments; ++Index)
		{
			const double AngleDegrees = StartAngleDegrees + (Index + 0.5) * 90.0 / CornerSegments;
			const double Angle = FMath::DegreesToRadians(AngleDegrees);
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			const FVector Center(CenterEnu.X, CenterEnu.Y, 0.0);
			const double Heading = FMath::Fmod(360.0 - AngleDegrees, 360.0);
			const FString Suffix = FString::Printf(TEXT("_%02d"), Index);
			// Chord-aligned boxes overlap slightly, eliminating sub-sample seams.
			// Size tangent overlap for the OUTER edge, not the centerline chord.
			// A centerline-sized slab leaves triangular holes at outer tire paths.
			const double RoadLength = 2.0 * (CornerRadius + RoadWidth * 0.5)
				* FMath::Sin(HalfStep) + 0.16;
			AddRoad(Layout, Prefix + TEXT("_Road") + Suffix,
				Center + Radial * CornerRadius * FMath::Cos(HalfStep),
				RoadLength, Heading, 0.0, RoadWidth + 0.04);
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const double CurbRadius = CornerRadius + Side * (RoadWidth * 0.5 + CurbWidth * 0.5);
				const double WalkRadius = CornerRadius + Side * (RoadWidth * 0.5 + CurbWidth + SidewalkWidth * 0.5);
				const FString SideId = Side < 0 ? TEXT("_Inner") : TEXT("_Outer");
				AddCurb(Layout, Prefix + SideId + TEXT("Curb") + Suffix,
					Center + Radial * CurbRadius * FMath::Cos(HalfStep),
					2.0 * CurbRadius * FMath::Sin(HalfStep) + 0.08, Heading);
				AddSidewalk(Layout, Prefix + SideId + TEXT("Sidewalk") + Suffix,
					Center + Radial * WalkRadius * FMath::Cos(HalfStep),
					2.0 * WalkRadius * FMath::Sin(HalfStep) + 0.15, Heading);
			}
		}
	}

	void AddCheckpoint(FLayout& Layout, const FVector& Position, double Heading)
	{
		if (!Layout.DriveRoute.IsEmpty()
			&& Layout.DriveRoute.Last().PositionEnuM.Equals(Position, 1.0e-7))
		{
			// The outgoing heading is authoritative at a straight/arc junction.
			Layout.DriveRoute.Last().HeadingDegrees = Heading;
			return;
		}
		FCheckpoint& Point = Layout.DriveRoute.AddDefaulted_GetRef();
		Point.PositionEnuM = Position;
		Point.HeadingDegrees = Heading;
	}

	void AddRouteStraight(FLayout& Layout, FVector Start, FVector End, double Heading, bool bEastGrade = false)
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(Start, End) / 1.8));
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			FVector Point = FMath::Lerp(Start, End, static_cast<double>(Index) / Steps);
			if (bEastGrade)
			{
				Point.Z = RampHeight(Point.Y);
			}
			AddCheckpoint(Layout, Point, Heading);
		}
	}

	void AddRouteCorner(FLayout& Layout, const FVector2D& Center, double StartAngle)
	{
		constexpr int32 Steps = 18;
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			const double AngleDegrees = StartAngle + Index * 90.0 / Steps;
			const double Angle = FMath::DegreesToRadians(AngleDegrees);
			AddCheckpoint(Layout,
				FVector(Center.X + LaneCornerRadius * FMath::Cos(Angle),
					Center.Y + LaneCornerRadius * FMath::Sin(Angle), 0.0),
				FMath::Fmod(720.0 - AngleDegrees, 360.0));
		}
	}

	void AddBuilding(FLayout& Layout, int32 Index, double East, double North, double Height)
	{
		const FString Prefix = FString::Printf(TEXT("Building_%02d"), Index);
		AddBox(Layout, Prefix, FVector(East, North, Height * 0.5),
			FVector(24.0, 22.0, Height), 0.0, EPalette::Building, false, true);
		AddBox(Layout, Prefix + TEXT("_Roof"), FVector(East, North, Height + 0.15),
			FVector(24.6, 22.6, 0.30), 0.0, EPalette::Sidewalk);
		for (int32 Floor = 0; Floor < FMath::FloorToInt(Height / 3.0); ++Floor)
		{
			const double Up = 1.7 + Floor * 3.0;
			for (int32 Face = -1; Face <= 1; Face += 2)
			{
				AddBox(Layout, Prefix + FString::Printf(TEXT("_Glass_E%d_F%d"), Face, Floor),
					FVector(East + Face * 11.015, North, Up),
					FVector(20.0, 0.035, 1.25), 0.0, EPalette::Glass);
				AddBox(Layout, Prefix + FString::Printf(TEXT("_Glass_N%d_F%d"), Face, Floor),
					FVector(East, North + Face * 12.015, Up),
					FVector(0.035, 18.0, 1.25), 0.0, EPalette::Glass);
			}
		}
	}
}

FLayout BuildLayout()
{
	FLayout Layout;
	Layout.GroundCenterEnuM = FVector(0.0, 80.0, 0.0);
	Layout.GroundHalfExtentNorthEastM = FVector2D(100.0, 120.0);
	Layout.Boxes.Reserve(650);
	Layout.DriveRoute.Reserve(420);
	AddBox(Layout, TEXT("Ground_Base"), FVector(0.0, 80.0, -0.58),
		FVector(200.0, 240.0, 1.0), 0.0, EPalette::Ground, true);

	AddRoad(Layout, TEXT("Road_South"), FVector(0.0, 2.0, 0.0), 152.12, 90.0);
	AddRoad(Layout, TEXT("Road_North"), FVector(0.0, 142.0, 0.0), 152.12, 90.0);
	AddRoad(Layout, TEXT("Road_West"), FVector(-92.0, 72.0, 0.0), 108.12, 0.0);
	const double GradeLimits[] = {18.0, 40.0, 60.0, 80.0, 100.0, 126.0};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const double North = (GradeLimits[Index] + GradeLimits[Index + 1]) * 0.5;
		const double Pitch = Index == 1 ? RampDegrees : (Index == 3 ? -RampDegrees : 0.0);
		const double Length = GradeLimits[Index + 1] - GradeLimits[Index];
		AddRoad(Layout, FString::Printf(TEXT("Road_East_%d"), Index),
			FVector(92.0, North, RampHeight(North)), Length + 0.06, 0.0, Pitch);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			AddSidewalk(Layout, FString::Printf(TEXT("Sidewalk_East_%d_%d"), Side, Index),
				FVector(92.0 + Side * 5.55, North, RampHeight(North)), Length + 0.02, 0.0, Pitch);
		}
	}
	AddCorner(Layout, TEXT("Corner_SE"), FVector2D(76.0, 18.0), -90.0);
	AddCorner(Layout, TEXT("Corner_NE"), FVector2D(76.0, 126.0), 0.0);
	AddCorner(Layout, TEXT("Corner_NW"), FVector2D(-76.0, 126.0), 90.0);
	AddCorner(Layout, TEXT("Corner_SW"), FVector2D(-76.0, 18.0), 180.0);

	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		AddCurb(Layout, FString::Printf(TEXT("Curb_South_%d"), Side),
			FVector(0.0, 2.0 + Side * 4.15, 0.0), 152.0, 90.0);
		AddSidewalk(Layout, FString::Printf(TEXT("Sidewalk_South_%d"), Side),
			FVector(0.0, 2.0 + Side * 5.55, 0.0), 152.0, 90.0);
		AddCurb(Layout, FString::Printf(TEXT("Curb_West_%d"), Side),
			FVector(-92.0 + Side * 4.15, 72.0, 0.0), 108.0, 0.0);
		AddSidewalk(Layout, FString::Printf(TEXT("Sidewalk_West_%d"), Side),
			FVector(-92.0 + Side * 5.55, 72.0, 0.0), 108.0, 0.0);
		for (int32 Half = -1; Half <= 1; Half += 2)
		{
			// Leave a 12m opening for the northern T-junction and crosswalk.
			AddCurb(Layout, FString::Printf(TEXT("Curb_North_%d_%d"), Side, Half),
				FVector(Half * 41.0, 142.0 + Side * 4.15, 0.0), 70.0, 90.0);
			AddSidewalk(Layout, FString::Printf(TEXT("Sidewalk_North_%d_%d"), Side, Half),
				FVector(Half * 41.0, 142.0 + Side * 5.55, 0.0), 70.0, 90.0);
		}
		// Short upright colliders follow the grade without unsupported pitched OBBs.
		for (int32 Index = 0; Index < 54; ++Index)
		{
			const double North = 19.0 + Index * 2.0;
			AddCurb(Layout, FString::Printf(TEXT("Curb_East_%d_%02d"), Side, Index),
				FVector(92.0 + Side * 4.15, North, RampHeight(North)), 2.0, 0.0);
		}
	}

	AddRoad(Layout, TEXT("Road_NorthBranch"), FVector(0.0, 156.0, 0.0), 28.2, 0.0);
	AddRoad(Layout, TEXT("Road_StoppingBay"), FVector(0.0, 168.0, 0.0), 10.0, 90.0, 0.0, 8.0);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		AddCurb(Layout, FString::Printf(TEXT("Curb_Branch_%d"), Side),
			FVector(Side * 4.15, 155.0, 0.0), 16.0, 0.0);
		AddSidewalk(Layout, FString::Printf(TEXT("Sidewalk_Branch_%d"), Side),
			FVector(Side * 5.55, 155.0, 0.0), 16.0, 0.0);
	}
	AddCurb(Layout, TEXT("Curb_BayEnd"), FVector(0.0, 172.15, 0.0), 10.0, 90.0);
	AddBox(Layout, TEXT("Barrier_BayEnd"), FVector(0.0, 173.0, 0.50),
		FVector(10.0, 0.35, 1.0), 90.0, EPalette::Barrier, false, true);

	// Simple center dashes, not lane-graph or traffic-control implementation.
	for (int32 Index = 0; Index < 18; ++Index)
	{
		const double East = -70.0 + Index * 8.0;
		AddTopSlab(Layout, FString::Printf(TEXT("Mark_South_%02d"), Index),
			FVector(East, 2.0, 0.014), 4.0, 0.14, 0.018, 90.0, 0.0, EPalette::Yellow);
		if (FMath::Abs(East) > 8.0)
		{
			AddTopSlab(Layout, FString::Printf(TEXT("Mark_North_%02d"), Index),
				FVector(East, 142.0, 0.014), 4.0, 0.14, 0.018, 90.0, 0.0, EPalette::Yellow);
		}
	}
	for (int32 Index = 0; Index < 13; ++Index)
	{
		const double North = 22.0 + Index * 8.0;
		AddTopSlab(Layout, FString::Printf(TEXT("Mark_West_%02d"), Index),
			FVector(-92.0, North, 0.014), 4.0, 0.14, 0.018, 0.0, 0.0, EPalette::Yellow);
		const double Pitch = North > 40.0 && North < 60.0 ? RampDegrees
			: (North > 80.0 && North < 100.0 ? -RampDegrees : 0.0);
		AddTopSlab(Layout, FString::Printf(TEXT("Mark_East_%02d"), Index),
			FVector(92.0, North, RampHeight(North) + 0.014), 3.8, 0.14, 0.018,
			0.0, Pitch, EPalette::Yellow);
	}
	for (int32 Index = 0; Index < 8; ++Index)
	{
		AddTopSlab(Layout, FString::Printf(TEXT("Crosswalk_%d"), Index),
			FVector(-3.5 + Index, 149.0, 0.014), 3.0, 0.48, 0.018,
			0.0, 0.0, EPalette::Marking);
	}
	AddTopSlab(Layout, TEXT("Branch_StopLine"), FVector(-2.0, 152.0, 0.014),
		3.6, 0.30, 0.018, 90.0, 0.0, EPalette::Marking);
	AddTopSlab(Layout, TEXT("Start_Line"), FVector(0.0, 0.0, 0.014),
		3.5, 0.30, 0.018, 0.0, 0.0, EPalette::Marking);

	for (int32 Row = 0; Row < 2; ++Row)
	{
		for (int32 Column = 0; Column < 4; ++Column)
		{
			const int32 Index = Row * 4 + Column;
			AddBuilding(Layout, Index, -60.0 + Column * 40.0,
				39.0 + Row * 65.0, 7.0 + ((Index * 3) % 4) * 3.0);
		}
	}
	AddBox(Layout, TEXT("Park_Island"), FVector(0.0, 72.0, -0.005),
		FVector(23.0, 130.0, 0.15), 0.0, EPalette::Grass);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		AddBox(Layout, FString::Printf(TEXT("Boundary_EastWest_%d"), Side),
			FVector(Side * 119.0, 80.0, 0.60), FVector(198.0, 0.50, 1.20),
			0.0, EPalette::Barrier, false, true);
		AddBox(Layout, FString::Printf(TEXT("Boundary_NorthSouth_%d"), Side),
			FVector(0.0, 80.0 + Side * 99.0, 0.60), FVector(238.0, 0.50, 1.20),
			90.0, EPalette::Barrier, false, true);
	}

	AddRouteStraight(Layout, FVector(0.0, 0.0, 0.0), FVector(76.0, 0.0, 0.0), 90.0);
	AddRouteCorner(Layout, FVector2D(76.0, 18.0), -90.0);
	AddRouteStraight(Layout, FVector(94.0, 18.0, 0.0), FVector(94.0, 126.0, 0.0), 0.0, true);
	AddRouteCorner(Layout, FVector2D(76.0, 126.0), 0.0);
	AddRouteStraight(Layout, FVector(76.0, 144.0, 0.0), FVector(-76.0, 144.0, 0.0), 270.0);
	AddRouteCorner(Layout, FVector2D(-76.0, 126.0), 90.0);
	AddRouteStraight(Layout, FVector(-94.0, 126.0, 0.0), FVector(-94.0, 18.0, 0.0), 180.0);
	AddRouteCorner(Layout, FVector2D(-76.0, 18.0), 180.0);
	AddRouteStraight(Layout, FVector(-76.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0), 90.0);
	return Layout;
}
}
