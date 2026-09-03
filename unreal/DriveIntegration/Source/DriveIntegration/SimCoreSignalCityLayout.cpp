#include "SimCoreSignalCityLayout.h"

#include "Algo/Reverse.h"

namespace SimCoreSignalCity
{
namespace
{
	using SimCoreVirtualCity::EPalette;
	using SimCoreVirtualCity::FCheckpoint;
	using SimCoreVirtualCity::FLayout;

	constexpr double RoadThicknessM = 0.24;
	constexpr double StreetWidthM = 10.0;
	constexpr double CurbWidthM = 0.30;
	constexpr double CurbHeightM = 0.24;
	constexpr double SidewalkWidthM = 2.40;
	constexpr double SidewalkThicknessM = 0.22;

	FVector ForwardEnu(double HeadingDegrees)
	{
		const double Heading = FMath::DegreesToRadians(HeadingDegrees);
		return FVector(FMath::Sin(Heading), FMath::Cos(Heading), 0.0);
	}

	FVector RightEnu(double HeadingDegrees)
	{
		const double Heading = FMath::DegreesToRadians(HeadingDegrees);
		return FVector(FMath::Cos(Heading), -FMath::Sin(Heading), 0.0);
	}

	double HeadingFor(const FVector& Start, const FVector& End)
	{
		const FVector Delta = End - Start;
		return FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Delta.X, Delta.Y)) + 360.0, 360.0);
	}

	void AddBox(FLayout& Layout, const FString& Id, const FVector& Center,
		const FVector& Size, double Heading, EPalette Palette,
		bool bGround = false, bool bStaticCollider = false)
	{
		SimCoreVirtualCity::FBox& Box = Layout.Boxes.AddDefaulted_GetRef();
		Box.Id = FName(*Id);
		Box.CenterEnuM = Center;
		Box.SizeM = Size;
		Box.HeadingDegrees = Heading;
		Box.Palette = Palette;
		Box.bGround = bGround;
		Box.bStaticCollider = bStaticCollider;
	}

	void AddTopSlab(FLayout& Layout, const FString& Id, const FVector& TopCenter,
		double Length, double Width, double Thickness, double Heading,
		EPalette Palette, bool bGround = false, bool bStaticCollider = false)
	{
		AddBox(Layout, Id, TopCenter - FVector(0.0, 0.0, Thickness * 0.5),
			FVector(Length, Width, Thickness), Heading, Palette, bGround, bStaticCollider);
	}

	void AddRoad(FLayout& Layout, const FString& Id, const FVector& Start,
		const FVector& End, double Width = StreetWidthM, double OverlapM = 0.18)
	{
		const double Heading = HeadingFor(Start, End);
		AddTopSlab(Layout, Id, (Start + End) * 0.5,
			FVector::Dist2D(Start, End) + OverlapM, Width, RoadThicknessM,
			Heading, EPalette::Road, true);
	}

	void AddStreetSegment(FLayout& Layout, const FString& Id,
		const FVector& Start, const FVector& End, double Width = StreetWidthM,
		double RoadOverlapM = 0.18, double EdgeStartInsetM = 0.0,
		double EdgeEndInsetM = 0.0)
	{
		const double Heading = HeadingFor(Start, End);
		const double Length = FVector::Dist2D(Start, End);
		check(EdgeStartInsetM >= 0.0 && EdgeEndInsetM >= 0.0
			&& EdgeStartInsetM + EdgeEndInsetM < Length);
		const FVector Forward = ForwardEnu(Heading);
		const double EdgeLength = Length - EdgeStartInsetM - EdgeEndInsetM;
		const FVector EdgeCenter = (Start + End) * 0.5
			+ Forward * ((EdgeStartInsetM - EdgeEndInsetM) * 0.5);
		const FVector Right = RightEnu(Heading);
		AddRoad(Layout, Id + TEXT("_Road"), Start, End, Width, RoadOverlapM);
		for (int32 Side : {-1, 1})
		{
			const FString SideName = Side < 0 ? TEXT("L") : TEXT("R");
			const FVector CurbCenter = EdgeCenter + Right * Side * (Width * 0.5 + CurbWidthM * 0.5);
			AddBox(Layout, Id + TEXT("_Curb_") + SideName,
				CurbCenter + FVector(0.0, 0.0, CurbHeightM * 0.5),
				FVector(EdgeLength + 0.12, CurbWidthM, CurbHeightM), Heading,
				EPalette::Curb, true, true);
			const FVector WalkCenter = EdgeCenter + Right * Side
				* (Width * 0.5 + CurbWidthM + SidewalkWidthM * 0.5);
			AddTopSlab(Layout, Id + TEXT("_Sidewalk_") + SideName,
				WalkCenter + FVector(0.0, 0.0, CurbHeightM),
				EdgeLength + 0.12, SidewalkWidthM, SidewalkThicknessM, Heading,
				EPalette::Sidewalk, true);
		}
	}

	TArray<FVector> Cubic(const FVector& A, const FVector& B,
		const FVector& C, const FVector& D, int32 Segments)
	{
		TArray<FVector> Points;
		Points.Reserve(Segments + 1);
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const double T = static_cast<double>(Index) / Segments;
			const double S = 1.0 - T;
			Points.Add(A * (S * S * S) + B * (3.0 * S * S * T)
				+ C * (3.0 * S * T * T) + D * (T * T * T));
		}
		return Points;
	}

	void AddStreetPolyline(FLayout& Layout, const FString& Prefix,
		const TArray<FVector>& Points, double Width = StreetWidthM,
		double RoadOverlapM = 0.18)
	{
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FString Id = Prefix + FString::Printf(TEXT("_%02d"), Index - 1);
			if (Index <= 3 || Index >= Points.Num()-3)
			{
				// A collector joins another public road here. Curbs are held back
				// across the short merge fan; otherwise a later chord's angled curb
				// can become the highest saved collision over an offset traffic lane.
				AddRoad(Layout, Id + TEXT("_Road"), Points[Index-1], Points[Index],
					Width, RoadOverlapM);
			}
			else
			{
				AddStreetSegment(Layout, Id, Points[Index-1], Points[Index],
					Width, RoadOverlapM);
			}
		}
	}

	void AddStopLine(FLayout& Layout, const FString& Id,
		const FVector& Center, double ApproachHeading)
	{
		AddTopSlab(Layout, Id, Center + FVector(0.0, 0.0, 0.022),
			4.4, 0.34, 0.018, FMath::Fmod(ApproachHeading + 90.0, 360.0),
			EPalette::Marking);
	}

	void AddCrosswalk(FLayout& Layout, const FString& Prefix,
		const FVector& Center, double ApproachHeading)
	{
		const FVector Along = ForwardEnu(ApproachHeading);
		for (int32 Stripe = 0; Stripe < 6; ++Stripe)
		{
			const double Offset = (Stripe - 2.5) * 0.56;
			AddTopSlab(Layout,
				Prefix + FString::Printf(TEXT("_Stripe_%02d"), Stripe),
				Center + Along * Offset + FVector(0.0, 0.0, 0.022),
				StreetWidthM - 1.0, 0.38, 0.018,
				FMath::Fmod(ApproachHeading + 90.0, 360.0), EPalette::Marking);
		}
	}

	void AddDirectionArrow(FLayout& Layout, const FString& Prefix,
		const FVector& Center, double ApproachHeading)
	{
		const FVector Forward = ForwardEnu(ApproachHeading);
		AddTopSlab(Layout, Prefix + TEXT("_Stem"),
			Center + FVector(0.0, 0.0, 0.023), 2.2, 0.18, 0.020,
			ApproachHeading, EPalette::Marking);
		for (int32 Side : {-1, 1})
		{
			AddTopSlab(Layout,
				Prefix + (Side < 0 ? TEXT("_HeadL") : TEXT("_HeadR")),
				Center + Forward * 1.1 + FVector(0.0, 0.0, 0.023),
				0.95, 0.16, 0.020,
				FMath::Fmod(ApproachHeading + Side * 38.0 + 360.0, 360.0),
				EPalette::Marking);
		}
	}

	void AddApproachMarkings(FLayout& Layout, const FString& Prefix,
		const FVector& Intersection, double ApproachHeading, double StopDistance)
	{
		const FVector Forward = ForwardEnu(ApproachHeading);
		const FVector Right = RightEnu(ApproachHeading);
		const FVector StopCenter = Intersection - Forward * StopDistance + Right * 2.5;
		AddStopLine(Layout, Prefix + TEXT("_StopLine"), StopCenter, ApproachHeading);
		AddCrosswalk(Layout, Prefix + TEXT("_Crosswalk"),
			Intersection - Forward * 7.4, ApproachHeading);
		AddDirectionArrow(Layout, Prefix + TEXT("_Arrow"),
			StopCenter - Forward * 13.0, ApproachHeading);
	}

	void AddBuilding(FLayout& Layout, int32 Index, const FVector2D& Position,
		double Height, double Heading = 0.0)
	{
		const FString Prefix = FString::Printf(TEXT("Building_%02d"), Index);
		AddBox(Layout, Prefix, FVector(Position.X, Position.Y, Height * 0.5),
			FVector(17.0, 19.0, Height), Heading, EPalette::Building, false, true);
		AddBox(Layout, Prefix + TEXT("_Roof"), FVector(Position.X, Position.Y, Height + 0.14),
			FVector(17.5, 19.5, 0.28), Heading, EPalette::Sidewalk);
		AddBox(Layout, Prefix + TEXT("_Glass"), FVector(Position.X, Position.Y, Height * 0.58),
			FVector(17.15, 19.15, 1.1), Heading, EPalette::Glass);
	}

	void AddRoutePoint(FLayout& Layout, const FVector& Position, double Heading)
	{
		if (!Layout.DriveRoute.IsEmpty()
			&& Layout.DriveRoute.Last().PositionEnuM.Equals(Position, 1.0e-7))
		{
			Layout.DriveRoute.Last().HeadingDegrees = Heading;
			return;
		}
		FCheckpoint& Checkpoint = Layout.DriveRoute.AddDefaulted_GetRef();
		Checkpoint.PositionEnuM = Position;
		Checkpoint.HeadingDegrees = Heading;
	}

	void AddRoutePolyline(FLayout& Layout, const TArray<FVector>& Points)
	{
		for (int32 Segment = 1; Segment < Points.Num(); ++Segment)
		{
			const FVector Start = Points[Segment - 1];
			const FVector End = Points[Segment];
			const double Heading = HeadingFor(Start, End);
			const int32 Steps = FMath::Max(1,
				FMath::CeilToInt(FVector::Dist2D(Start, End) / 1.5));
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				AddRoutePoint(Layout,
					FMath::Lerp(Start, End, static_cast<double>(Step) / Steps), Heading);
			}
		}
	}
}

SimCoreVirtualCity::FLayout BuildLayout()
{
	FLayout Layout;
	Layout.GroundCenterEnuM = FVector(0.0, 80.0, 0.0);
	Layout.GroundHalfExtentNorthEastM = FVector2D(100.0, 120.0);
	Layout.Boxes.Reserve(360);
	Layout.DriveRoute.Reserve(420);
	AddBox(Layout, TEXT("Ground_Base"), FVector(0.0, 80.0, -0.58),
		FVector(200.0, 240.0, 1.0), 0.0, EPalette::Ground, true);

	const FVector MainIntersection(0.0, 40.0, 0.0);
	const FVector AuxiliaryIntersection(0.0, 120.0, 0.0);

	// The primary and offset northern crosses are separated into approach
	// slabs, leaving collider-free intersection plates between their curb ends.
	// Leave a real curb/sidewalk opening at each collector merge.  Keeping the
	// straight-street sidewalk across these mouths makes the saved-map raycast
	// hit Rough sidewalk above otherwise valid Asphalt collector support.
	constexpr double CollectorEdgeOpeningM = 12.0;
	AddStreetSegment(Layout, TEXT("SouthWest"), FVector(-105.0, 40.0, 0.0),
		FVector(-13.0, 40.0, 0.0), StreetWidthM, 0.18, CollectorEdgeOpeningM, 0.0);
	AddStreetSegment(Layout, TEXT("SouthEast"), FVector(13.0, 40.0, 0.0),
		FVector(105.0, 40.0, 0.0), StreetWidthM, 0.18, 0.0, CollectorEdgeOpeningM);
	AddStreetSegment(Layout, TEXT("NorthWest"), FVector(-90.0, 120.0, 0.0),
		FVector(-13.0, 120.0, 0.0), StreetWidthM, 0.18, CollectorEdgeOpeningM, 0.0);
	AddStreetSegment(Layout, TEXT("NorthEast"), FVector(13.0, 120.0, 0.0),
		FVector(100.0, 120.0, 0.0), StreetWidthM, 0.18, 0.0, CollectorEdgeOpeningM);
	AddStreetSegment(Layout, TEXT("CentralSouth"), FVector(0.0, -10.0, 0.0), FVector(0.0, 27.0, 0.0));
	AddStreetSegment(Layout, TEXT("CentralMiddle"), FVector(0.0, 53.0, 0.0), FVector(0.0, 106.0, 0.0));
	AddStreetSegment(Layout, TEXT("CentralNorth"), FVector(0.0, 134.0, 0.0), FVector(0.0, 168.0, 0.0));
	AddTopSlab(Layout, TEXT("Road_Intersection_Main"), MainIntersection,
		26.2, 26.2, RoadThicknessM, 0.0, EPalette::Road, true);
	AddTopSlab(Layout, TEXT("Road_Intersection_Auxiliary"), AuxiliaryIntersection,
		28.2, 28.2, RoadThicknessM, 0.0, EPalette::Road, true);
	// Broad, non-colliding asphalt aprons cover the tangent transition where a
	// two-way collector fans out into its two lane centers.  Without this small
	// overlap the outside half-width sample can fall behind the first chord even
	// though the authored centerlines meet exactly.
	AddTopSlab(Layout, TEXT("Road_Collector_WestSouthTransition"),
		FVector(-105.0, 40.0, 0.0), 16.0, 16.0, RoadThicknessM,
		0.0, EPalette::Road, true);
	AddTopSlab(Layout, TEXT("Road_Collector_EastSouthTransition"),
		FVector(105.0, 40.0, 0.0), 16.0, 16.0, RoadThicknessM,
		0.0, EPalette::Road, true);
	AddTopSlab(Layout, TEXT("Road_Collector_WestNorthTransition"),
		FVector(-90.0, 120.0, 0.0), 16.0, 16.0, RoadThicknessM,
		0.0, EPalette::Road, true);
	AddTopSlab(Layout, TEXT("Road_Collector_EastNorthTransition"),
		FVector(100.0, 120.0, 0.0), 16.0, 16.0, RoadThicknessM,
		0.0, EPalette::Road, true);

	const TArray<FVector> WestCurve = Cubic(FVector(-105.0, 40.0, 0.0),
		FVector(-112.0, 62.0, 0.0), FVector(-108.0, 101.0, 0.0),
		FVector(-90.0, 120.0, 0.0), 12);
	const TArray<FVector> EastCurve = Cubic(FVector(105.0, 40.0, 0.0),
		FVector(112.0, 68.0, 0.0), FVector(110.0, 101.0, 0.0),
		FVector(100.0, 120.0, 0.0), 14);
	// The bidirectional collectors carry two offset 3m traffic corridors through
	// changing curvature, so they use a 14m merge envelope at the chord joins.
	AddStreetPolyline(Layout, TEXT("CollectorWestCurve"), WestCurve, 14.0, 0.8);
	AddStreetPolyline(Layout, TEXT("CollectorEastCurve"), EastCurve, 14.0, 0.8);

	// Eight approaches receive a stop line, a full-width zebra crossing and a
	// directional arrow.  Signal heads remain runtime presentation, not collision.
	AddApproachMarkings(Layout, TEXT("Main_West_EB"), MainIntersection, 90.0, 10.5);
	AddApproachMarkings(Layout, TEXT("Main_East_WB"), MainIntersection, 270.0, 10.5);
	AddApproachMarkings(Layout, TEXT("Main_South_NB"), MainIntersection, 0.0, 10.5);
	AddApproachMarkings(Layout, TEXT("Main_North_SB"), MainIntersection, 180.0, 10.5);
	AddApproachMarkings(Layout, TEXT("Aux_South_NB"), AuxiliaryIntersection, 0.0, 11.5);
	AddApproachMarkings(Layout, TEXT("Aux_North_SB"), AuxiliaryIntersection, 180.0, 11.5);
	AddApproachMarkings(Layout, TEXT("Aux_West_EB"), AuxiliaryIntersection, 90.0, 11.5);
	AddApproachMarkings(Layout, TEXT("Aux_East_WB"), AuxiliaryIntersection, 270.0, 11.5);

	AddBox(Layout, TEXT("Park_West"), FVector(-50.0, 83.0, -0.04),
		FVector(34.0, 23.0, 0.10), 18.0, EPalette::Grass);
	AddBox(Layout, TEXT("Park_SouthEast"), FVector(45.0, 77.0, -0.04),
		FVector(28.0, 31.0, 0.10), -12.0, EPalette::Grass);
	AddBuilding(Layout, 0, FVector2D(-70.0, 10.0), 10.0, -8.0);
	AddBuilding(Layout, 1, FVector2D(-35.0, 11.0), 16.0, 5.0);
	AddBuilding(Layout, 2, FVector2D(36.0, 11.0), 13.0, -7.0);
	AddBuilding(Layout, 3, FVector2D(72.0, 75.0), 19.0, 12.0);
	AddBuilding(Layout, 4, FVector2D(-67.0, 88.0), 15.0, 9.0);
	AddBuilding(Layout, 5, FVector2D(65.0, 90.0), 11.0, -11.0);
	AddBuilding(Layout, 6, FVector2D(-34.0, 154.0), 20.0, 4.0);

	// No perimeter-wall rectangle: the visible/drivable network ends naturally.
	// The QA lap follows the two asymmetric collectors, crosses both junctions,
	// and closes on the west side of the main avenue.
	AddRoutePolyline(Layout, {FVector(-80.0, 40.0, 0.0), FVector(105.0, 40.0, 0.0)});
	AddRoutePolyline(Layout, EastCurve);
	AddRoutePolyline(Layout, {FVector(100.0, 120.0, 0.0), FVector(-90.0, 120.0, 0.0)});
	TArray<FVector> ReverseWest = WestCurve;
	Algo::Reverse(ReverseWest);
	AddRoutePolyline(Layout, ReverseWest);
	AddRoutePolyline(Layout, {FVector(-105.0, 40.0, 0.0), FVector(-80.0, 40.0, 0.0)});
	return Layout;
}
}
