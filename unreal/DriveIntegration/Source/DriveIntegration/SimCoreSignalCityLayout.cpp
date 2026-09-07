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
	constexpr double PaintTopM = 0.004;
	constexpr double PaintThicknessM = 0.002;

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

	void AddLanePaint(FLayout& Layout, const FString& Id, const FVector& Center,
		double Length, double Width, double Heading, EPalette Palette,
		bool bNaturalPaint, double LegacyTopM = 0.023,
		double LegacyThicknessM = 0.018)
	{
		AddTopSlab(Layout, Id,
			FVector(Center.X, Center.Y, bNaturalPaint ? PaintTopM : LegacyTopM),
			Length, Width, bNaturalPaint ? PaintThicknessM : LegacyThicknessM,
			Heading, Palette);
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
		double EdgeEndInsetM = 0.0, bool bCompleteLaneMarkings = false,
		bool bNaturalLaneMarkings = false)
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
		const double MarkingStartInsetM = bNaturalLaneMarkings ? EdgeStartInsetM : 0.0;
		const double MarkingEndInsetM = bNaturalLaneMarkings ? EdgeEndInsetM : 0.0;
		const double MarkingLength = Length - MarkingStartInsetM - MarkingEndInsetM;
		const FVector MarkingStart = Start + Forward * MarkingStartInsetM;
		const FVector MarkingCenter = (Start + End) * 0.5
			+ Forward * ((MarkingStartInsetM - MarkingEndInsetM) * 0.5);
		AddRoad(Layout, Id + TEXT("_Road"), Start, End, Width, RoadOverlapM);
		if (Width >= 19.0)
		{
			AddLanePaint(Layout, Id + TEXT("_Centerline"), MarkingCenter,
				MarkingLength, 0.18, Heading, EPalette::Yellow, bNaturalLaneMarkings);
			for (int32 Side : {-1, 1})
			{
				for (int32 LaneBoundary = 1; LaneBoundary <= 2; ++LaneBoundary)
				{
					for (int32 Dash = 0; 6.0 + Dash * 9.0 < MarkingLength - 4.0; ++Dash)
					{
						const FVector Center = MarkingStart + Forward * (6.0 + Dash * 9.0)
							+ Right * Side * (0.05 + 3.3 * LaneBoundary);
						AddLanePaint(Layout,
							Id + FString::Printf(TEXT("_LaneDivider_%d_%d_%02d"), Side, LaneBoundary, Dash),
							Center, 3.0, 0.14, Heading, EPalette::Marking,
							bNaturalLaneMarkings);
					}
				}
			}
		}
		else if (bCompleteLaneMarkings)
		{
			// The collectors are one lane in each direction.  Their road slabs are
			// deliberately wider than the traffic corridor at the merge fans, but
			// they still require a continuous yellow separator on every chord.
			AddLanePaint(Layout, Id + TEXT("_Centerline"), MarkingCenter,
				MarkingLength + FMath::Min(RoadOverlapM, 0.30), 0.18,
				Heading, EPalette::Yellow, bNaturalLaneMarkings);
		}
		if (bCompleteLaneMarkings)
		{
			// White edge lines make every authored lane visually bounded.  The
			// curved collectors retain their broad asphalt shoulder, so their edge
			// lines follow the actual +/-2m traffic centers rather than the 14m
			// support slab boundary.
			const double EdgeOffset = Width >= 19.0 ? Width * 0.5 - 0.24 : 3.65;
			for (int32 Side : {-1, 1})
			{
				AddLanePaint(Layout,
					Id + (Side < 0 ? TEXT("_EdgeLine_L") : TEXT("_EdgeLine_R")),
					MarkingCenter + Right * Side * EdgeOffset,
					MarkingLength + FMath::Min(RoadOverlapM, 0.30), 0.14,
					Heading, EPalette::Marking, bNaturalLaneMarkings);
			}
		}
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
		double RoadOverlapM = 0.18, bool bCompleteLaneMarkings = false,
		bool bNaturalLaneMarkings = false)
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
				if (bCompleteLaneMarkings && !bNaturalLaneMarkings)
				{
					const double Heading = HeadingFor(Points[Index-1], Points[Index]);
					const double Length = FVector::Dist2D(Points[Index-1], Points[Index]);
					const FVector Right = RightEnu(Heading);
					AddTopSlab(Layout, Id + TEXT("_Centerline"),
						(Points[Index-1] + Points[Index]) * 0.5 + FVector(0,0,0.023),
						Length + FMath::Min(RoadOverlapM, 0.30), 0.18, 0.018,
						Heading, EPalette::Yellow);
					for (int32 Side : {-1, 1})
					{
						AddTopSlab(Layout,
							Id + (Side < 0 ? TEXT("_EdgeLine_L") : TEXT("_EdgeLine_R")),
							(Points[Index-1] + Points[Index]) * 0.5
								+ Right * Side * 3.65 + FVector(0,0,0.023),
							Length + FMath::Min(RoadOverlapM, 0.30), 0.14, 0.018,
							Heading, EPalette::Marking);
					}
				}
			}
			else
			{
				AddStreetSegment(Layout, Id, Points[Index-1], Points[Index],
					Width, RoadOverlapM, 0.0, 0.0, bCompleteLaneMarkings,
					bNaturalLaneMarkings);
			}
		}
	}

	TArray<double> BuildArcStations(const TArray<FVector>& Points)
	{
		TArray<double> Stations{0.0};
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Stations.Add(Stations.Last() + FVector::Dist2D(Points[Index - 1], Points[Index]));
		}
		return Stations;
	}

	double CollectorTaperAt(double Station, double Length)
	{
		const double T = FMath::Clamp(FMath::Min(Station, Length - Station) / 24.0, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	void AddCollectorLaneMerges(FLayout& Layout, const FString& Prefix,
		const TArray<FVector>& Points, const TArray<double>& Stations,
		const TArray<FVector>& UnitRightPoints)
	{
		for (bool bSouth : {true, false})
		{
			const int32 First = bSouth ? 0 : Points.Num() - 1;
			const int32 Direction = bSouth ? 1 : -1;
			const FVector Incoming(Points[0].X > 0 ? 1.0 : -1.0, 0, 0);
			const FVector EntryRight(Incoming.Y, -Incoming.X, 0);
			for (int32 Boundary : {1, 2})
			{
				const double MergeDistance = Boundary == 2 ? 12.0 : 24.0;
				int32 Last = First;
				while (Last + Direction >= 0 && Last + Direction < Points.Num()
					&& FMath::Abs(Stations[Last] - Stations[First]) < MergeDistance)
				{
					Last += Direction;
				}
				const double EndDistance = FMath::Abs(Stations[Last] - Stations[First]);
				const double EntryOffset = 0.05 + 3.3 * Boundary;
				for (int32 Side : {-1, 1})
				{
					const FString Id = Prefix + (bSouth ? TEXT("_MergeSouth") : TEXT("_MergeNorth"))
						+ (Side < 0 ? TEXT("_L") : TEXT("_R")) + FString::Printf(TEXT("_%d"), Boundary);
					TArray<FVector> Paint;
					Paint.Add(Points[First] - Incoming * 6.0 + EntryRight * Side * EntryOffset);
					for (int32 Vertex = First; ; Vertex += Direction)
					{
						const double Distance = FMath::Abs(Stations[Vertex] - Stations[First]);
						const double T = FMath::Clamp(Distance / EndDistance, 0.0, 1.0);
						const double Blend = T * T * (3.0 - 2.0 * T);
						const double EdgeOffset = FMath::Lerp(9.76, 3.65,
							CollectorTaperAt(Stations[Vertex], Stations.Last()));
						const FVector TravelRight = (UnitRightPoints[Vertex] - Points[Vertex]) * Direction;
						Paint.Add(Points[Vertex] + TravelRight * Side * FMath::Lerp(EntryOffset, EdgeOffset, Blend));
						if (Vertex == Last) break;
					}
					for (int32 Index = 1; Index < Paint.Num(); ++Index)
					{
						AddLanePaint(Layout, Id + FString::Printf(TEXT("_%02d"), Index - 1),
							(Paint[Index - 1] + Paint[Index]) * 0.5,
							FVector::Dist2D(Paint[Index - 1], Paint[Index]) + 0.08,
							0.14, HeadingFor(Paint[Index - 1], Paint[Index]), EPalette::Marking, true);
					}
				}
			}
		}
	}

	void AddSharedCollector(FLayout& Layout, const FString& Prefix,
		const TArray<FVector>& Points, bool bSealJoints, bool bLaneMerges)
	{
		const TArray<double> Stations = BuildArcStations(Points);
		const TArray<FVector> UnitRightPoints = OffsetCollectorCenterline(Points, 1.0);
		auto TaperAt = [&Stations](double Station)
		{
			return CollectorTaperAt(Station, Stations.Last());
		};
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FString Id = Prefix + FString::Printf(TEXT("_%03d"), Index - 1);
			const double Width = FMath::Lerp(20.0, StreetWidthM,
				TaperAt((Stations[Index - 1] + Stations[Index]) * 0.5));
			const double Heading = HeadingFor(Points[Index - 1], Points[Index]);
			const double Length = FVector::Dist2D(Points[Index - 1], Points[Index]);
			const FVector Center = (Points[Index - 1] + Points[Index]) * 0.5;
			const FVector Forward = (Points[Index] - Points[Index - 1]).GetSafeNormal2D();
			double MaximumHalfAngleTangent = 0.0;
			for (int32 Adjacent : {Index - 1, Index + 1})
			{
				if (Adjacent <= 0 || Adjacent >= Points.Num()) continue;
				const FVector Other = (Points[Adjacent] - Points[Adjacent - 1]).GetSafeNormal2D();
				const double Cross = FMath::Abs(Forward.X * Other.Y - Forward.Y * Other.X);
				MaximumHalfAngleTangent = FMath::Max(MaximumHalfAngleTangent,
					Cross / FMath::Max(0.01, 1.0 + FVector::DotProduct(Forward, Other)));
			}
			// A fixed overlap leaves wedge-shaped holes at the outside of a bend.
			// Extend each slab to the miter intersection of its neighbouring edges.
			const double RoadOverlap = bSealJoints
				? FMath::Max(0.24, Width * MaximumHalfAngleTangent + 0.08) : 0.24;
			AddRoad(Layout, Id + TEXT("_Road"), Points[Index - 1], Points[Index], Width, RoadOverlap);
			AddLanePaint(Layout, Id + TEXT("_Centerline"), Center,
				Length + 0.08, 0.18, Heading, EPalette::Yellow, true);
			for (int32 Side : {-1, 1})
			{
				const FString Suffix = Side < 0 ? TEXT("_L") : TEXT("_R");
				auto OffsetPoint = [&](int32 Vertex, double WideOffset, double NarrowOffset)
				{
					return Points[Vertex] + (UnitRightPoints[Vertex] - Points[Vertex]) * Side
						* FMath::Lerp(WideOffset, NarrowOffset, TaperAt(Stations[Vertex]));
				};
				const FVector EdgeStart = OffsetPoint(Index - 1, 9.76, 3.65);
				const FVector EdgeEnd = OffsetPoint(Index, 9.76, 3.65);
				AddLanePaint(Layout, Id + TEXT("_EdgeLine") + Suffix,
					(EdgeStart + EdgeEnd) * 0.5, FVector::Dist2D(EdgeStart, EdgeEnd) + 0.08,
					0.14, HeadingFor(EdgeStart, EdgeEnd), EPalette::Marking, true);
				const FVector CurbStart = OffsetPoint(Index - 1, 10.0 + CurbWidthM * 0.5, 5.0 + CurbWidthM * 0.5);
				const FVector CurbEnd = OffsetPoint(Index, 10.0 + CurbWidthM * 0.5, 5.0 + CurbWidthM * 0.5);
				AddBox(Layout, Id + TEXT("_Curb") + Suffix,
					(CurbStart + CurbEnd) * 0.5 + FVector(0, 0, CurbHeightM * 0.5),
					FVector(FVector::Dist2D(CurbStart, CurbEnd) + 0.12, CurbWidthM, CurbHeightM),
					HeadingFor(CurbStart, CurbEnd),
					EPalette::Curb, true, true);
				const double WalkOffset = CurbWidthM + SidewalkWidthM * 0.5;
				const FVector WalkStart = OffsetPoint(Index - 1, 10.0 + WalkOffset, 5.0 + WalkOffset);
				const FVector WalkEnd = OffsetPoint(Index, 10.0 + WalkOffset, 5.0 + WalkOffset);
				AddTopSlab(Layout, Id + TEXT("_Sidewalk") + Suffix,
					(WalkStart + WalkEnd) * 0.5 + FVector(0, 0, CurbHeightM),
					FVector::Dist2D(WalkStart, WalkEnd) + 0.12, SidewalkWidthM,
					SidewalkThicknessM, HeadingFor(WalkStart, WalkEnd),
					EPalette::Sidewalk, true);
			}
		}
		if (bLaneMerges) { AddCollectorLaneMerges(Layout, Prefix, Points, Stations, UnitRightPoints); }
	}

	void AddRaisedCollectorTransitionMarkings(FLayout& Layout, const FString& Id,
		const FVector& Center, const FVector& StraightArm, const FVector& CollectorArm)
	{
		const FVector StraightForward = StraightArm.GetSafeNormal2D();
		const FVector CollectorForward = CollectorArm.GetSafeNormal2D();
		check(!StraightForward.IsNearlyZero() && !CollectorForward.IsNearlyZero());
		const FVector StraightRight(StraightForward.Y, -StraightForward.X, 0.0);
		const FVector CollectorRight(CollectorForward.Y, -CollectorForward.X, 0.0);
		auto AddConnector = [&Layout, &Id](const FString& Suffix,
			const FVector& Start, const FVector& End, double Width, EPalette Palette)
		{
			AddTopSlab(Layout, Id + Suffix,
				(Start + End) * 0.5 + FVector(0.0, 0.0, 0.023),
				FVector::Dist2D(Start, End) + 0.18, Width, 0.018,
				HeadingFor(Start, End), Palette);
		};

		// The broad approach and two-lane collector use different edge offsets.
		// Join their outer boundaries across the asphalt merge apron instead of
		// leaving an unmarked square between the adjoining road sections.
		constexpr double ApproachEdgeOffsetM = 9.76;
		constexpr double CollectorEdgeOffsetM = 3.65;
		for (int32 Side : {-1, 1})
		{
			AddConnector(Side < 0 ? TEXT("_EdgeLine_L") : TEXT("_EdgeLine_R"),
				Center + StraightRight * Side * ApproachEdgeOffsetM,
				Center - CollectorRight * Side * CollectorEdgeOffsetM,
				0.14, EPalette::Marking);
		}
		// The adjoining yellow separators already terminate at Center. This short
		// bevel closes their point contact without drawing a line through a junction.
		AddConnector(TEXT("_Centerline"), Center + StraightForward * 0.55,
			Center + CollectorForward * 0.55, 0.18, EPalette::Yellow);
	}

	void AddPaintPolyline(FLayout& Layout, const FString& Prefix,
		const TArray<FVector>& Points, double Width, EPalette Palette)
	{
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector& Start = Points[Index - 1];
			const FVector& End = Points[Index];
			AddLanePaint(Layout,
				Prefix + FString::Printf(TEXT("_%02d"), Index - 1),
				(Start + End) * 0.5, FVector::Dist2D(Start, End) + 0.08,
				Width, HeadingFor(Start, End), Palette, true);
		}
	}

	void AddNaturalCollectorTransitionMarkings(FLayout& Layout, const FString& Id,
		const FVector& Center, const FVector& StraightArm,
		const FVector& CollectorJoin, const FVector& CollectorArm,
		bool bAlignSidesWithTravel)
	{
		const FVector StraightForward = StraightArm.GetSafeNormal2D();
		const FVector CollectorForward = CollectorArm.GetSafeNormal2D();
		check(!StraightForward.IsNearlyZero() && !CollectorForward.IsNearlyZero());
		const FVector StraightRight(StraightForward.Y, -StraightForward.X, 0.0);
		const FVector CollectorRight(CollectorForward.Y, -CollectorForward.X, 0.0);
		constexpr double ApproachInsetM = 12.0;
		constexpr double ApproachEdgeOffsetM = 9.76;
		constexpr double CollectorEdgeOffsetM = 3.65;
		constexpr int32 TransitionSegments = 6;
		for (int32 Side : {-1, 1})
		{
			// The taper is authored from the straight road back toward the merge,
			// so its travel direction is -StraightForward.  The previous v2 layout
			// offset this endpoint in +StraightForward's left/right frame while the
			// collector endpoint used the taper travel frame.  Left and right then
			// exchanged places through the curve and visibly crossed.  Express both
			// ends in the same travel frame so the three paint lines stay ordered.
			const double StraightSideSign = bAlignSidesWithTravel ? -1.0 : 1.0;
			const FVector Start = Center + StraightForward * ApproachInsetM
				+ StraightRight * Side * ApproachEdgeOffsetM * StraightSideSign;
			const FVector End = CollectorJoin
				+ CollectorRight * Side * CollectorEdgeOffsetM;
			AddPaintPolyline(Layout,
				Id + (Side < 0 ? TEXT("_EdgeLine_L") : TEXT("_EdgeLine_R")),
				Cubic(Start, Start - StraightForward * 7.0,
					End - CollectorForward * 7.0, End, TransitionSegments),
				0.14, EPalette::Marking);
		}
		const FVector Start = Center + StraightForward * ApproachInsetM;
		AddPaintPolyline(Layout, Id + TEXT("_Centerline"),
			Cubic(Start, Start - StraightForward * 7.0,
				CollectorJoin - CollectorForward * 7.0, CollectorJoin,
				TransitionSegments),
			0.18, EPalette::Yellow);
	}

	void AddStopLine(FLayout& Layout, const FString& Id,
		const FVector& Center, double ApproachHeading, double Width,
		bool bNaturalLaneMarkings)
	{
		AddLanePaint(Layout, Id, Center, Width, 0.34,
			FMath::Fmod(ApproachHeading + 90.0, 360.0), EPalette::Marking,
			bNaturalLaneMarkings, 0.022, 0.018);
	}

	void AddCrosswalk(FLayout& Layout, const FString& Prefix,
		const FVector& Center, double ApproachHeading, double Width,
		bool bNaturalLaneMarkings)
	{
		const FVector Along = ForwardEnu(ApproachHeading);
		for (int32 Stripe = 0; Stripe < 6; ++Stripe)
		{
			const double Offset = (Stripe - 2.5) * 0.56;
			AddLanePaint(Layout,
				Prefix + FString::Printf(TEXT("_Stripe_%02d"), Stripe),
				Center + Along * Offset, Width - 1.0, 0.38,
				FMath::Fmod(ApproachHeading + 90.0, 360.0), EPalette::Marking,
				bNaturalLaneMarkings, 0.022, 0.018);
		}
	}

	void AddDirectionArrow(FLayout& Layout, const FString& Prefix,
		const FVector& Center, double ApproachHeading, bool bNaturalLaneMarkings)
	{
		const FVector Forward = ForwardEnu(ApproachHeading);
		AddLanePaint(Layout, Prefix + TEXT("_Stem"), Center,
			2.2, 0.18, ApproachHeading, EPalette::Marking,
			bNaturalLaneMarkings, 0.023, 0.020);
		for (int32 Side : {-1, 1})
		{
			AddLanePaint(Layout,
				Prefix + (Side < 0 ? TEXT("_HeadL") : TEXT("_HeadR")),
				Center + Forward * 1.1, 0.95, 0.16,
				FMath::Fmod(ApproachHeading + Side * 38.0 + 360.0, 360.0),
				EPalette::Marking, bNaturalLaneMarkings, 0.023, 0.020);
		}
	}

	void AddApproachMarkings(FLayout& Layout, const FString& Prefix,
		const FVector& Intersection, double ApproachHeading, double StopDistance,
		bool bThreeLane, bool bSafeCrossings, bool bNaturalLaneMarkings)
	{
		const FVector Forward = ForwardEnu(ApproachHeading);
		const FVector Right = RightEnu(ApproachHeading);
		const FVector StopCenter = Intersection - Forward * (bSafeCrossings ? 18.5 : StopDistance) + Right * (bThreeLane ? 5.0 : 2.5);
		AddStopLine(Layout, Prefix + TEXT("_StopLine"), StopCenter,
			ApproachHeading, bThreeLane ? 9.6 : 4.4, bNaturalLaneMarkings);
		AddCrosswalk(Layout, Prefix + TEXT("_Crosswalk"),
			Intersection - Forward * (bSafeCrossings ? 15.0 : 7.4),
			ApproachHeading, bThreeLane ? 20.0 : StreetWidthM,
			bNaturalLaneMarkings);
		AddDirectionArrow(Layout, Prefix + TEXT("_Arrow"),
			StopCenter - Forward * 13.0, ApproachHeading, bNaturalLaneMarkings);
		if (bThreeLane)
		{
			for (int32 Turn : {-1, 1})
			{
				const FString Name = Prefix + (Turn < 0 ? TEXT("_Left_Arrow") : TEXT("_Right_Arrow"));
				const FVector Center = StopCenter - Forward * 13.0 + Right * Turn * 3.3;
				AddLanePaint(Layout, Name + TEXT("_Stem"), Center, 2.0, 0.18,
					ApproachHeading, EPalette::Marking, bNaturalLaneMarkings, 0.023, 0.020);
				AddLanePaint(Layout, Name + TEXT("_Bend"),
					Center + Forward * 1.0 + Right * Turn * 0.6,
					1.4, 0.18, ApproachHeading + Turn * 90.0, EPalette::Marking,
					bNaturalLaneMarkings, 0.023, 0.020);
				for (int32 Side : {-1, 1})
				{
					AddLanePaint(Layout,
						Name + (Side < 0 ? TEXT("_HeadL") : TEXT("_HeadR")),
						Center + Forward * 1.0 + Right * Turn * 1.2, 0.9, 0.16,
						FMath::Fmod(ApproachHeading + Turn * 90.0 + Side * 38.0 + 360.0, 360.0),
						EPalette::Marking, bNaturalLaneMarkings, 0.023, 0.020);
				}
			}
		}
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

	void AddDistantCityBackdrop(FLayout& Layout)
	{
		// This plane and skyline are deliberately visual-only. They hide the hard
		// authoring boundary without enlarging the collision bake or drivable area.
		AddTopSlab(Layout, TEXT("Backdrop_HorizonGround"),
			FVector(0.0, 80.0, -0.10), 720.0, 720.0, 0.08, 0.0,
			EPalette::Ground);
		struct FBackdropBuilding
		{
			double East;
			double North;
			double Width;
			double Depth;
			double Height;
		};
		const FBackdropBuilding Buildings[] = {
			{-260,-120,42,36,54}, {-190,-150,34,42,72}, {-115,-165,38,32,43},
			{ 105,-170,44,38,58}, { 180,-145,36,46,76}, { 260,-105,48,34,49},
			{-285,  10,36,50,68}, {-275, 110,46,38,44}, {-250, 220,34,42,82},
			{ 285,  25,42,36,63}, { 275, 125,38,48,48}, { 250, 235,46,38,74},
			{-205, 340,40,44,61}, {-115, 360,34,38,79}, { -35, 350,46,34,52},
			{  50, 360,36,42,70}, { 130, 345,42,36,46}, { 215, 330,34,48,67}
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Buildings); ++Index)
		{
			const FBackdropBuilding& Building = Buildings[Index];
			AddBox(Layout, FString::Printf(TEXT("Backdrop_Skyline_%02d"), Index),
				FVector(Building.East, Building.North, Building.Height * 0.5 - 0.10),
				FVector(Building.Width, Building.Depth, Building.Height),
				(Index % 3 - 1) * 6.0, EPalette::Building);
		}
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

static TArray<FVector> BuildCollectorCenterlineInternal(bool bEast, bool bMinimumRadius)
{
	if (bMinimumRadius)
	{
		constexpr double Radius = 15.0;
		const double Side = bEast ? 1.0 : -1.0;
		const double StartEast = bEast ? 93.0 : -93.0;
		const double EndEast = bEast ? 88.0 : -78.0;
		TArray<FVector> Points;
		for (int32 Index = 0; Index <= 32; ++Index)
		{
			const double Angle = -UE_PI * 0.5 + UE_PI * 0.5 * Index / 32.0;
			Points.Add(FVector(StartEast + Side * Radius * FMath::Cos(Angle),
				55.0 + Radius * FMath::Sin(Angle), 0.0));
		}
		const FVector Entry(StartEast + Side * Radius, 55, 0);
		const FVector Exit(EndEast + Side * Radius, 105, 0);
		const TArray<FVector> Middle = Cubic(Entry, Entry + FVector(0, 17, 0),
			Exit - FVector(0, 17, 0), Exit, 44);
		for (int32 Index = 1; Index < Middle.Num(); ++Index) { Points.Add(Middle[Index]); }
		for (int32 Index = 1; Index <= 32; ++Index)
		{
			const double Angle = UE_PI * 0.5 * Index / 32.0;
			Points.Add(FVector(EndEast + Side * Radius * FMath::Cos(Angle),
				105.0 + Radius * FMath::Sin(Angle), 0.0));
		}
		// Junctions are exact shared vertices, independent of trigonometric rounding.
		Points[0] = FVector(StartEast, 40.0, 0.0);
		Points.Last() = FVector(EndEast, 120.0, 0.0);
		return Points;
	}
	const TArray<FVector> Middle = bEast
		? Cubic(FVector(105, 40, 0), FVector(112, 68, 0),
			FVector(110, 101, 0), FVector(100, 120, 0), 64)
		: Cubic(FVector(-105, 40, 0), FVector(-112, 62, 0),
			FVector(-108, 101, 0), FVector(-90, 120, 0), 64);
	const FVector Start(bEast ? 93 : -93, 40, 0);
	const FVector End(bEast ? 88 : -78, 120, 0);
	const FVector Incoming(bEast ? 1 : -1, 0, 0);
	constexpr int32 Entry = 10;
	constexpr int32 Exit = 54;
	const FVector EntryForward = (Middle[Entry + 1] - Middle[Entry - 1]).GetSafeNormal2D();
	const FVector ExitForward = (Middle[Exit + 1] - Middle[Exit - 1]).GetSafeNormal2D();
	TArray<FVector> Points = Cubic(Start, Start + Incoming * 18.0,
		Middle[Entry] - EntryForward * 8.0, Middle[Entry], 32);
	for (int32 Index = Entry + 1; Index <= Exit; ++Index) { Points.Add(Middle[Index]); }
	const TArray<FVector> Last = Cubic(Middle[Exit], Middle[Exit] + ExitForward * 8.0,
		End + Incoming * 18.0, End, 32);
	for (int32 Index = 1; Index < Last.Num(); ++Index) { Points.Add(Last[Index]); }
	return Points;
}

TArray<FVector> BuildCollectorCenterline(bool bEast)
{
	return BuildCollectorCenterlineInternal(bEast, true);
}

TArray<FVector> OffsetCollectorCenterline(const TArray<FVector>& Centerline, double OffsetM)
{
	TArray<FVector> Points;
	if (Centerline.Num() < 2) { return Points; }
	Points.Reserve(Centerline.Num());
	for (int32 Index = 0; Index < Centerline.Num(); ++Index)
	{
		FVector Forward = (Centerline[FMath::Min(Index + 1, Centerline.Num() - 1)]
			- Centerline[FMath::Max(Index - 1, 0)]).GetSafeNormal2D();
		if (Index == 0) { Forward = FVector(Centerline[0].X > 0 ? 1 : -1, 0, 0); }
		else if (Index == Centerline.Num() - 1) { Forward = FVector(Centerline[0].X > 0 ? -1 : 1, 0, 0); }
		Points.Add(Centerline[Index] + FVector(Forward.Y, -Forward.X, 0) * OffsetM);
	}
	return Points;
}

static SimCoreVirtualCity::FLayout BuildLayoutInternal(
	bool bThreeLane, bool bSafeCrossings, bool bCompleteLaneMarkings,
	bool bNaturalLaneMarkings = false, bool bDistantBackdrop = false,
	bool bAlignCollectorTransitionSides = false, bool bSharedCollectors = false,
	bool bSealCollectorJoints = true, bool bMinimumCollectorRadius = true,
	bool bCollectorLaneMerges = false)
{
	FLayout Layout;
	const double ApproachRoadWidthM = bThreeLane ? 20.0 : 10.0;
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
	const double CollectorEdgeOpeningM = bSharedCollectors ? 0.0 : 12.0;
	const double CollectorInsetM = bSharedCollectors ? 12.0 : 0.0;
	AddStreetSegment(Layout, TEXT("SouthWest"), FVector(-105.0 + CollectorInsetM, 40.0, 0.0),
		FVector(-13.0, 40.0, 0.0), ApproachRoadWidthM, 0.18,
		CollectorEdgeOpeningM, 0.0, bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetSegment(Layout, TEXT("SouthEast"), FVector(13.0, 40.0, 0.0),
		FVector(105.0 - CollectorInsetM, 40.0, 0.0), ApproachRoadWidthM, 0.18,
		0.0, CollectorEdgeOpeningM, bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetSegment(Layout, TEXT("NorthWest"), FVector(-90.0 + CollectorInsetM, 120.0, 0.0),
		FVector(-13.0, 120.0, 0.0), ApproachRoadWidthM, 0.18,
		CollectorEdgeOpeningM, 0.0, bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetSegment(Layout, TEXT("NorthEast"), FVector(13.0, 120.0, 0.0),
		FVector(100.0 - CollectorInsetM, 120.0, 0.0), ApproachRoadWidthM, 0.18,
		0.0, CollectorEdgeOpeningM, bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetSegment(Layout, TEXT("CentralSouth"), FVector(0.0, -10.0, 0.0),
		FVector(0.0, 27.0, 0.0), ApproachRoadWidthM, 0.18, 0.0, 0.0,
		bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetSegment(Layout, TEXT("CentralMiddle"), FVector(0.0, 53.0, 0.0),
		FVector(0.0, 106.0, 0.0), ApproachRoadWidthM, 0.18, 0.0, 0.0,
		bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetSegment(Layout, TEXT("CentralNorth"), FVector(0.0, 134.0, 0.0),
		FVector(0.0, 168.0, 0.0), ApproachRoadWidthM, 0.18, 0.0, 0.0,
		bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddTopSlab(Layout, TEXT("Road_Intersection_Main"), MainIntersection,
		26.2, 26.2, RoadThicknessM, 0.0, EPalette::Road, true);
	AddTopSlab(Layout, TEXT("Road_Intersection_Auxiliary"), AuxiliaryIntersection,
		28.2, 28.2, RoadThicknessM, 0.0, EPalette::Road, true);
	// Broad, non-colliding asphalt aprons cover the tangent transition where a
	// two-way collector fans out into its two lane centers.  Without this small
	// overlap the outside half-width sample can fall behind the first chord even
	// though the authored centerlines meet exactly.
	if (!bSharedCollectors)
	{
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

	}
	const TArray<FVector> WestCurve = bSharedCollectors
		? BuildCollectorCenterlineInternal(false, bMinimumCollectorRadius)
		: Cubic(FVector(-105.0, 40.0, 0.0),
		FVector(-112.0, 62.0, 0.0), FVector(-108.0, 101.0, 0.0),
		FVector(-90.0, 120.0, 0.0), 12);
	const TArray<FVector> EastCurve = bSharedCollectors
		? BuildCollectorCenterlineInternal(true, bMinimumCollectorRadius)
		: Cubic(FVector(105.0, 40.0, 0.0),
		FVector(112.0, 68.0, 0.0), FVector(110.0, 101.0, 0.0),
		FVector(100.0, 120.0, 0.0), 14);
	// The old 14m collector exposed an implausibly broad asphalt shoulder. The
	// current 10m carriageway still supports both 3.2m traffic footprints while
	// reading as an ordinary two-way urban road.
	const double CollectorWidthM = bNaturalLaneMarkings ? StreetWidthM : 14.0;
	if (bSharedCollectors)
	{
		AddSharedCollector(Layout, TEXT("CollectorWestCurve"), WestCurve, bSealCollectorJoints, bCollectorLaneMerges);
		AddSharedCollector(Layout, TEXT("CollectorEastCurve"), EastCurve, bSealCollectorJoints, bCollectorLaneMerges);
	}
	else
	{
	AddStreetPolyline(Layout, TEXT("CollectorWestCurve"), WestCurve,
		CollectorWidthM, 0.8, bCompleteLaneMarkings, bNaturalLaneMarkings);
	AddStreetPolyline(Layout, TEXT("CollectorEastCurve"), EastCurve,
		CollectorWidthM, 0.8, bCompleteLaneMarkings, bNaturalLaneMarkings);
	if (bCompleteLaneMarkings)
	{
		if (bNaturalLaneMarkings)
		{
			AddNaturalCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_WestSouthTransition"), WestCurve[0],
				FVector::ForwardVector, WestCurve[3], WestCurve[4] - WestCurve[2],
				bAlignCollectorTransitionSides);
			AddNaturalCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_EastSouthTransition"), EastCurve[0],
				-FVector::ForwardVector, EastCurve[3], EastCurve[4] - EastCurve[2],
				bAlignCollectorTransitionSides);
			AddNaturalCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_WestNorthTransition"), WestCurve.Last(),
				FVector::ForwardVector, WestCurve[WestCurve.Num()-4],
				WestCurve[WestCurve.Num()-5] - WestCurve[WestCurve.Num()-3],
				bAlignCollectorTransitionSides);
			AddNaturalCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_EastNorthTransition"), EastCurve.Last(),
				-FVector::ForwardVector, EastCurve[EastCurve.Num()-4],
				EastCurve[EastCurve.Num()-5] - EastCurve[EastCurve.Num()-3],
				bAlignCollectorTransitionSides);
		}
		else
		{
			AddRaisedCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_WestSouthTransition"), WestCurve[0],
				FVector::ForwardVector, WestCurve[1] - WestCurve[0]);
			AddRaisedCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_EastSouthTransition"), EastCurve[0],
				-FVector::ForwardVector, EastCurve[1] - EastCurve[0]);
			AddRaisedCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_WestNorthTransition"), WestCurve.Last(),
				FVector::ForwardVector, WestCurve[WestCurve.Num()-2] - WestCurve.Last());
			AddRaisedCollectorTransitionMarkings(Layout,
				TEXT("Road_Collector_EastNorthTransition"), EastCurve.Last(),
				-FVector::ForwardVector, EastCurve[EastCurve.Num()-2] - EastCurve.Last());
		}
	}
	}

	// Eight approaches receive a stop line, a full-width zebra crossing and a
	// directional arrow.  Signal heads remain runtime presentation, not collision.
	AddApproachMarkings(Layout, TEXT("Main_West_EB"), MainIntersection, 90.0, 10.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Main_East_WB"), MainIntersection, 270.0, 10.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Main_South_NB"), MainIntersection, 0.0, 10.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Main_North_SB"), MainIntersection, 180.0, 10.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Aux_South_NB"), AuxiliaryIntersection, 0.0, 11.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Aux_North_SB"), AuxiliaryIntersection, 180.0, 11.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Aux_West_EB"), AuxiliaryIntersection, 90.0, 11.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);
	AddApproachMarkings(Layout, TEXT("Aux_East_WB"), AuxiliaryIntersection, 270.0, 11.5,
		bThreeLane, bSafeCrossings, bNaturalLaneMarkings);

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
	if (bDistantBackdrop) { AddDistantCityBackdrop(Layout); }

	// No perimeter-wall rectangle: the visible/drivable network ends naturally.
	// The QA lap follows the two asymmetric collectors, crosses both junctions,
	// and closes on the west side of the main avenue.
	AddRoutePolyline(Layout, {FVector(-80.0, 40.0, 0.0), EastCurve[0]});
	AddRoutePolyline(Layout, EastCurve);
	AddRoutePolyline(Layout, {EastCurve.Last(), WestCurve.Last()});
	TArray<FVector> ReverseWest = WestCurve;
	Algo::Reverse(ReverseWest);
	AddRoutePolyline(Layout, ReverseWest);
	AddRoutePolyline(Layout, {WestCurve[0], FVector(-80.0, 40.0, 0.0)});
	return Layout;
}
SimCoreVirtualCity::FLayout BuildLayout()
{
	return BuildLayoutInternal(true, true, true, true, true, true, true, true, true, true);
}
SimCoreVirtualCity::FLayout BuildUnmergedCollectorLanesV6Layout()
{
	return BuildLayoutInternal(true, true, true, true, true, true, true);
}
SimCoreVirtualCity::FLayout BuildAlignedCollectorMarkingsV3Layout()
{
	return BuildLayoutInternal(true, true, true, true, true, true);
}
SimCoreVirtualCity::FLayout BuildUnsealedCollectorCurvesV4Layout()
{
	return BuildLayoutInternal(true, true, true, true, true, true, true, false, false);
}
SimCoreVirtualCity::FLayout BuildSealedCollectorCurvesV5Layout()
{
	return BuildLayoutInternal(true, true, true, true, true, true, true, true, false);
}
SimCoreVirtualCity::FLayout BuildLegacySingleLaneLayout() { return BuildLayoutInternal(false, false, false); }
SimCoreVirtualCity::FLayout BuildInitialThreeLaneLayout() { return BuildLayoutInternal(true, false, false); }
SimCoreVirtualCity::FLayout BuildIncompleteLaneMarkingsLayout() { return BuildLayoutInternal(true, true, false); }
SimCoreVirtualCity::FLayout BuildRaisedLaneMarkingsLayout()
{
	return BuildLayoutInternal(true, true, true, false, false);
}
SimCoreVirtualCity::FLayout BuildNaturalLaneMarkingsV2Layout()
{
	return BuildLayoutInternal(true, true, true, true, true, false);
}
}
