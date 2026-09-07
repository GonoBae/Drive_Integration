#include "SimCoreSignalCityTrafficLayout.h"

#include "SimCoreSignalCityLayout.h"
#include "SimCoreVirtualCityLayout.h"
#include "Algo/Reverse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

namespace SimCoreSignalCity
{
namespace
{
	using SimCoreVirtualCity::EPalette;
	using SimCoreVirtualCity::FBox;
	using SimCoreVirtualCity::FLayout;

	bool IsFinite(const FVector& Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z);
	}

	bool GroundTop(const FLayout& Source, const FVector& Point, bool bRoadOnly, double& Top)
	{
		bool bFound = false;
		Top = -DBL_MAX;
		for (const FBox& Box : Source.Boxes)
		{
			if (!Box.bGround || (bRoadOnly && Box.Palette != EPalette::Road)) { continue; }
			const FQuat Rotation = FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0.0).Quaternion();
			const FVector Center(Box.CenterEnuM.Y, Box.CenterEnuM.X, Box.CenterEnuM.Z);
			const FVector Normal = Rotation.RotateVector(FVector::UpVector);
			if (Normal.Z <= 0.0) { continue; }
			const double Height = Center.Z + (Box.SizeM.Z * 0.5
				- Normal.X * (Point.Y - Center.X) - Normal.Y * (Point.X - Center.Y)) / Normal.Z;
			const FVector Local = Rotation.UnrotateVector(FVector(Point.Y, Point.X, Height) - Center);
			if (FMath::Abs(Local.X) <= Box.SizeM.X * 0.5 + 1.e-6
				&& FMath::Abs(Local.Y) <= Box.SizeM.Y * 0.5 + 1.e-6)
			{
				Top = FMath::Max(Top, Height);
				bFound = true;
			}
		}
		return bFound;
	}

	void Point(TArray<FVector>& Points, const FVector2D& Value)
	{
		const FVector PointValue(Value.X, Value.Y, 0.0);
		if (Points.IsEmpty() || !Points.Last().Equals(PointValue, 1.e-7)) { Points.Add(PointValue); }
	}

	void Straight(TArray<FVector>& Points, const FVector2D& Start, const FVector2D& End)
	{
		const int32 Count = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Start, End)));
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			Point(Points, FMath::Lerp(Start, End, static_cast<double>(Index) / Count));
		}
	}

	void Cubic(TArray<FVector>& Points, const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D, int32 MinimumSamples = 18)
	{
		const double ControlLength = FVector2D::Distance(A, B)
			+ FVector2D::Distance(B, C) + FVector2D::Distance(C, D);
		const int32 Count = FMath::Max(MinimumSamples, FMath::CeilToInt(ControlLength * 0.75));
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			const double T = static_cast<double>(Index) / Count;
			const double S = 1.0 - T;
			Point(Points, A * (S*S*S) + B * (3.0*S*S*T)
				+ C * (3.0*S*T*T) + D * (T*T*T));
		}
	}

	FTrafficLane& Add(FTrafficLayout& Layout, uint32 Id, TArray<uint32> Successors,
		uint32 Group = 0, double Speed = 8.333333)
	{
		FTrafficLane& Lane = Layout.Lanes.AddDefaulted_GetRef();
		Lane.Id = Id;
		Lane.Successors = MoveTemp(Successors);
		Lane.SignalGroupId = Group;
		Lane.bTerminal = Lane.Successors.IsEmpty();
		Lane.SpeedLimitMps = Speed;
		return Lane;
	}

	void AddStraight(FTrafficLayout& Layout, uint32 Id, FVector2D Start, FVector2D End,
		TArray<uint32> Successors, uint32 Group = 0, double Speed = 8.333333)
	{
		Straight(Add(Layout, Id, MoveTemp(Successors), Group, Speed).PointsEnuM, Start, End);
	}

	void AddCubic(FTrafficLayout& Layout, uint32 Id, FVector2D A, FVector2D B,
		FVector2D C, FVector2D D, TArray<uint32> Successors, double Speed = 6.944444)
	{
		Cubic(Add(Layout, Id, MoveTemp(Successors), 0, Speed).PointsEnuM, A, B, C, D);
	}

	void AddCollector(FTrafficLayout& Layout, uint32 Id,
		bool bEast, bool bReverse, uint32 Successor)
	{
		TArray<FVector> Points = OffsetCollectorCenterline(BuildCollectorCenterline(bEast), bReverse ? -2.0 : 2.0);
		if (bReverse) { Algo::Reverse(Points); }
		Add(Layout, Id, {Successor}, 0, 6.944444).PointsEnuM = MoveTemp(Points);
	}

	void AddMovement(FTrafficLayout& Layout, uint32 Id, FVector2D A, FVector2D B,
		FVector2D C, FVector2D D, uint32 Successor)
	{
		Cubic(Add(Layout, Id, {Successor}, 0, 4.166667).PointsEnuM, A, B, C, D, 16);
	}

	double ArcLength(const TArray<FVector>& Points, int32 EndIndex = INDEX_NONE)
	{
		const int32 End = EndIndex == INDEX_NONE ? Points.Num() - 1 : EndIndex;
		double Result = 0.0;
		for (int32 Index = 1; Index <= End; ++Index)
		{
			Result += FVector::Dist(Points[Index-1], Points[Index]);
		}
		return Result;
	}

	void AddDedicatedApproach(FTrafficLayout& Layout, uint32 SourceId)
	{
		const int32 SourceIndex = Layout.Lanes.IndexOfByPredicate(
			[SourceId](const FTrafficLane& Lane) { return Lane.Id == SourceId; });
		check(SourceIndex != INDEX_NONE);
		const TArray<FVector> Prior = Layout.Lanes[SourceIndex].PointsEnuM;
		const FVector Travel = (Prior.Last() - Prior[0]).GetSafeNormal2D();
		const FVector Stop = Prior.Last() - Travel * 8.0;
		TArray<FVector> Original;
		// Controlled endpoints must precede the outer crosswalk, not sit inside
		// the widened perpendicular carriageway. Connectors inherit this move below.
		Straight(Original, FVector2D(Prior[0].X, Prior[0].Y), FVector2D(Stop.X, Stop.Y));
		const TArray<uint32> Movements = Layout.Lanes[SourceIndex].Successors;
		check(Movements.Num() == 3);
		FTrafficLane Center = Layout.Lanes[SourceIndex];
		Center.PointsEnuM = Original;
		FTrafficLane Left = Center, RightLane = Center;
		Left.Id = SourceId == 1034 ? 2035 : SourceId + 5;
		RightLane.Id = SourceId == 1034 ? 2036 : SourceId + 6;
		Center.WidthM = Left.WidthM = RightLane.WidthM = 3.2;
		Center.Successors = {Movements[0]};
		Left.Successors = {Movements[2]};
		RightLane.Successors = {Movements[1]};
		const double Length = FVector::Dist2D(Original[0], Original.Last());
		const FVector Forward = (Original.Last() - Original[0]).GetSafeNormal2D();
		const FVector Right(Forward.Y, -Forward.X, 0.0);
		int32 BeginIndex = 0;
		int32 EndIndex = Original.Num()-1;
		for (int32 Index = 0; Index < Original.Num(); ++Index)
		{
			const double Progress = FVector::Dist2D(Original[0], Original[Index]);
			// One bounded entry fan preserves the existing collector join. At the
			// stop line all three centers stay distinct; each has its own legal
			// movement, not three arrows over one merged centerline.
			const double T = FMath::Clamp(Progress / 10.0, 0.0, 1.0);
			const double Blend = T*T*T*(10.0 + T*(-15.0 + 6.0*T));
			Center.PointsEnuM[Index] = Original[Index] + Right * (3.0 * Blend);
			Left.PointsEnuM[Index] = Original[Index] - Right * (0.3 * Blend);
			RightLane.PointsEnuM[Index] = Original[Index] + Right * (6.3 * Blend);
			if (Progress <= 14.0 + 1.e-6) { BeginIndex = Index; }
			if (Progress <= Length - 8.0 + 1.e-6) { EndIndex = Index; }
		}
		for (FTrafficLane* Neighbor : {&Left, &RightLane})
		{
			const double Begin = ArcLength(Center.PointsEnuM, BeginIndex), End = ArcLength(Center.PointsEnuM, EndIndex);
			const double OtherBegin = ArcLength(Neighbor->PointsEnuM, BeginIndex), OtherEnd = ArcLength(Neighbor->PointsEnuM, EndIndex);
			Center.LaneChanges.Add({Neighbor->Id, Begin, End, OtherBegin, OtherEnd});
			Neighbor->LaneChanges.Add({Center.Id, OtherBegin, OtherEnd, Begin, End});
		}
		// Every inbound predecessor can choose the left/straight/right lane at
		// its common fan entrance. Mid-approach passing still requires adjacency.
		for (FTrafficLane& Lane : Layout.Lanes)
		{
			if (Lane.Successors.Contains(SourceId)) { Lane.Successors.Add(Left.Id); Lane.Successors.Add(RightLane.Id); }
		}
		for (const FTrafficLane* Approach : {&Center, &Left, &RightLane})
		{
			FTrafficLane* Movement = Layout.Lanes.FindByPredicate(
				[&](const FTrafficLane& Lane) { return Lane.Id == Approach->Successors[0]; });
			check(Movement);
			const FVector Delta = Approach->PointsEnuM.Last() - Movement->PointsEnuM[0];
			for (int32 Index = 0; Index < Movement->PointsEnuM.Num(); ++Index)
			{
				const double T = static_cast<double>(Index) / (Movement->PointsEnuM.Num() - 1);
				const double Blend = 1.0 - T*T*T*(10.0 + T*(-15.0 + 6.0*T));
				Movement->PointsEnuM[Index] += Delta * Blend;
			}
		}
		Layout.Lanes[SourceIndex] = MoveTemp(Center);
		Layout.Lanes.Add(MoveTemp(Left));
		Layout.Lanes.Add(MoveTemp(RightLane));
	}

	FVector SampleArc(const FTrafficLane& Lane, double Station, FVector* Tangent = nullptr)
	{
		for (int32 Index = 1; Index < Lane.PointsEnuM.Num(); ++Index)
		{
			const FVector A = Lane.PointsEnuM[Index-1];
			const FVector B = Lane.PointsEnuM[Index];
			const double Span = FVector::Dist(A, B);
			if (Station <= Span || Index == Lane.PointsEnuM.Num()-1)
			{
				if (Tangent) { *Tangent = (B-A).GetSafeNormal2D(); }
				return FMath::Lerp(A, B, FMath::Clamp(Station / Span, 0.0, 1.0));
			}
			Station -= Span;
		}
		return FVector::ZeroVector;
	}

	bool CheckChecksum(const FString& Checksum)
	{
		if (Checksum.Len() != 24 || !Checksum.StartsWith(TEXT("fnv1a64:"))) { return false; }
		for (int32 Index = 8; Index < Checksum.Len(); ++Index)
		{
			const TCHAR C = Checksum[Index];
			if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) { return false; }
		}
		return true;
	}
}

FTrafficLayout BuildTrafficLayout()
{
	FTrafficLayout Result;
	Result.Lanes.Reserve(58);
	Result.Signals.Reserve(16);

	// South intersection, east/west approaches and all permissive movements.
	AddStraight(Result, 1010, {-93,38}, {-10,38}, {1011,1012,1013}, 101);
	AddStraight(Result, 1011, {-10,38}, {10,38}, {1014}, 0, 6.944444);
	AddMovement(Result, 1012, {-10,38}, {-5,38}, {-2,35}, {-2,30}, 1044);
	AddMovement(Result, 1013, {-10,38}, {0,38}, {2,40}, {2,50}, 1034);
	AddStraight(Result, 1014, {10,38}, {93,38}, {3001});

	AddStraight(Result, 1020, {93,42}, {10,42}, {1021,1022,1023}, 103);
	AddStraight(Result, 1021, {10,42}, {-10,42}, {1024}, 0, 6.944444);
	AddMovement(Result, 1022, {10,42}, {5,42}, {2,45}, {2,50}, 1034);
	AddMovement(Result, 1023, {10,42}, {0,42}, {-2,40}, {-2,30}, 1044);
	AddStraight(Result, 1024, {-10,42}, {-93,42}, {3002});

	// Central avenue. The middle approaches are controlled by the other junction.
	AddStraight(Result, 1030, {2,-10}, {2,30}, {1031,1032,1033}, 102);
	AddStraight(Result, 1031, {2,30}, {2,50}, {1034}, 0, 6.944444);
	AddMovement(Result, 1032, {2,30}, {2,35}, {5,38}, {10,38}, 1014);
	AddMovement(Result, 1033, {2,30}, {2,40}, {0,42}, {-10,42}, 1024);
	AddStraight(Result, 1034, {2,50}, {2,110}, {2031,2032,2033}, 202);

	AddStraight(Result, 2040, {-2,168}, {-2,130}, {2041,2042,2043}, 204);
	AddStraight(Result, 2041, {-2,130}, {-2,110}, {1040}, 0, 6.944444);
	AddMovement(Result, 2042, {-2,130}, {-2,125}, {-5,122}, {-10,122}, 2024);
	AddMovement(Result, 2043, {-2,130}, {-2,120}, {0,118}, {10,118}, 2014);
	AddStraight(Result, 1040, {-2,110}, {-2,50}, {1041,1042,1043}, 104);
	AddStraight(Result, 1041, {-2,50}, {-2,30}, {1044}, 0, 6.944444);
	AddMovement(Result, 1042, {-2,50}, {-2,45}, {-5,42}, {-10,42}, 1024);
	AddMovement(Result, 1043, {-2,50}, {-2,40}, {0,38}, {10,38}, 1014);
	AddStraight(Result, 1044, {-2,30}, {-2,-10}, {});

	// North intersection, again with explicit straight/right/left connectors.
	AddStraight(Result, 2010, {-78,118}, {-10,118}, {2011,2012,2013}, 201);
	AddStraight(Result, 2011, {-10,118}, {10,118}, {2014}, 0, 6.944444);
	AddMovement(Result, 2012, {-10,118}, {-5,118}, {-2,115}, {-2,110}, 1040);
	AddMovement(Result, 2013, {-10,118}, {0,118}, {2,120}, {2,130}, 2034);
	AddStraight(Result, 2014, {10,118}, {88,118}, {3003});

	AddStraight(Result, 2020, {88,122}, {10,122}, {2021,2022,2023}, 203);
	AddStraight(Result, 2021, {10,122}, {-10,122}, {2024}, 0, 6.944444);
	AddMovement(Result, 2022, {10,122}, {5,122}, {2,125}, {2,130}, 2034);
	AddMovement(Result, 2023, {10,122}, {0,122}, {-2,120}, {-2,110}, 1040);
	AddStraight(Result, 2024, {-10,122}, {-78,122}, {3004});

	AddStraight(Result, 2031, {2,110}, {2,130}, {2034}, 0, 6.944444);
	AddMovement(Result, 2032, {2,110}, {2,115}, {5,118}, {10,118}, 2014);
	AddMovement(Result, 2033, {2,110}, {2,120}, {0,122}, {-10,122}, 2024);
	AddStraight(Result, 2034, {2,130}, {2,168}, {});

	// Two asymmetric, bidirectional collectors close the urban blocks without
	// falling back to the old map's rectangular perimeter road.
	AddCollector(Result, 3001, true, false, 2020);
	AddCollector(Result, 3002, false, false, 2010);
	AddCollector(Result, 3003, true, true, 1020);
	AddCollector(Result, 3004, false, true, 1010);

	for (uint32 Approach : {1010u, 1020u, 1030u, 1040u, 2010u, 2020u, 1034u, 2040u})
	{
		AddDedicatedApproach(Result, Approach);
	}

	Result.Signals = {
		{1, 1, 101, FVector(-18,28.5,0), 90},
		{2, 1, 103, FVector(18,51.5,0), 270},
		{3, 1, 102, FVector(11.5,22,0), 0},
		{4, 1, 104, FVector(-11.5,58,0), 180},
		{5, 2, 201, FVector(-18,108.5,0), 90},
		{6, 2, 203, FVector(18,131.5,0), 270},
		{7, 2, 202, FVector(11.5,102,0), 0},
		{8, 2, 204, FVector(-11.5,138,0), 180},
		// All vehicle approaches are red during the exclusive WALK phase.
		{101, 1, 105, FVector(-11.5,25,0), 90, ETrafficSignalKind::Pedestrian},
		{102, 1, 105, FVector(11.5,25,0), 270, ETrafficSignalKind::Pedestrian},
		{103, 1, 106, FVector(15,28.5,0), 0, ETrafficSignalKind::Pedestrian},
		{104, 1, 106, FVector(15,51.5,0), 180, ETrafficSignalKind::Pedestrian},
		{105, 2, 205, FVector(-11.5,105,0), 90, ETrafficSignalKind::Pedestrian},
		{106, 2, 205, FVector(11.5,105,0), 270, ETrafficSignalKind::Pedestrian},
		{107, 2, 206, FVector(15,108.5,0), 0, ETrafficSignalKind::Pedestrian},
		{108, 2, 206, FVector(15,131.5,0), 180, ETrafficSignalKind::Pedestrian},
	};

	const auto MakePlan = [](uint32 Id, uint32 Offset, uint32 Base)
	{
		FSignalPlan Plan;
		Plan.Id = Id;
		Plan.OffsetMs = Offset;
		Plan.Groups = {Base+1, Base+2, Base+3, Base+4, Base+5, Base+6};
		Plan.Phases = {{2000, {}, {}}};
		// Protected approach: left/straight/right of one incoming direction
		// share green; opposing approaches and every pedestrian remain red.
		for (uint32 Group : {Base+1, Base+3, Base+2, Base+4})
		{
			Plan.Phases.Add({9000, {Group}, {}});
			Plan.Phases.Add({2000, {}, {Group}});
			Plan.Phases.Add({1500, {}, {}});
		}
		Plan.Phases.Add({18000, {Base+5, Base+6}, {}});
		Plan.Phases.Add({2000, {}, {}});
		return Plan;
	};
	Result.SignalPlans = {MakePlan(1, 0, 100), MakePlan(2, 14000, 200)};

	const FLayout Source = BuildLayout();
	for (FTrafficLane& Lane : Result.Lanes)
	{
		for (FVector& Position : Lane.PointsEnuM)
		{
			double Height = 0.0;
			if (GroundTop(Source, Position, true, Height)) { Position.Z = Height; }
		}
	}
	for (FTrafficSignal& Signal : Result.Signals)
	{
		double Height = 0.0;
		if (GroundTop(Source, Signal.PositionEnuM, false, Height)) { Signal.PositionEnuM.Z = Height; }
	}
	return Result;
}

bool ValidateTrafficLayout(const FTrafficLayout& Layout, FString& OutError)
{
	OutError.Reset();
	auto Fail = [&OutError](const FString& Message) { OutError = Message; return false; };
	if (Layout.Lanes.IsEmpty() || Layout.Lanes.Num() > 256
		|| Layout.Signals.Num() != 16 || Layout.SignalPlans.Num() != 2)
	{
		return Fail(TEXT("SignalCity requires 8 vehicle heads, 8 pedestrian heads and two controllers."));
	}
	TMap<uint32, const FTrafficLane*> Lanes;
	TMap<uint32, uint32> GroupControllers;
	TSet<uint32> PlanIds;
	for (const FSignalPlan& Plan : Layout.SignalPlans)
	{
		if (!Plan.Id || Plan.Id > 64 || PlanIds.Contains(Plan.Id)
			|| Plan.Groups.IsEmpty() || Plan.Groups.Num() > 16
			|| Plan.Phases.IsEmpty() || Plan.Phases.Num() > 32)
		{
			return Fail(TEXT("Invalid signal controller identity or resource count."));
		}
		PlanIds.Add(Plan.Id);
		TSet<uint32> PlanGroups;
		for (uint32 Group : Plan.Groups)
		{
			if (!Group || Group > 4096 || PlanGroups.Contains(Group) || GroupControllers.Contains(Group))
			{
				return Fail(TEXT("Signal groups must be nonzero and globally owned by one controller."));
			}
			PlanGroups.Add(Group);
			GroupControllers.Add(Group, Plan.Id);
		}
		uint64 CycleMs = 0;
		for (const FSignalPhase& Phase : Plan.Phases)
		{
			if (Phase.DurationMs < 100 || Phase.DurationMs > 120000)
			{
				return Fail(TEXT("Signal phase duration is outside 100..120000 ms."));
			}
			CycleMs += Phase.DurationMs;
			TSet<uint32> Seen;
			for (uint32 Group : Phase.GreenGroups)
			{
				if (!PlanGroups.Contains(Group) || Seen.Contains(Group))
				{
					return Fail(TEXT("Green phase references a foreign or duplicate group."));
				}
				Seen.Add(Group);
			}
			for (uint32 Group : Phase.YellowGroups)
			{
				if (!PlanGroups.Contains(Group) || Seen.Contains(Group))
				{
					return Fail(TEXT("Yellow phase conflicts with a green/duplicate group."));
				}
				Seen.Add(Group);
			}
		}
		if (CycleMs > 600000 || Plan.OffsetMs >= CycleMs)
		{
			return Fail(TEXT("Signal cycle or offset is outside the bounded contract."));
		}
	}

	int32 PointCount = 0;
	int32 GroundSamples = 0;
	const FLayout Source = BuildLayout();
	for (const FTrafficLane& Lane : Layout.Lanes)
	{
		if (!Lane.Id || Lanes.Contains(Lane.Id) || Lane.PointsEnuM.Num() < 2
			|| Lane.PointsEnuM.Num() > 2048 || !FMath::IsFinite(Lane.WidthM)
			|| Lane.WidthM < 2 || Lane.WidthM > 8
			|| !FMath::IsFinite(Lane.SpeedLimitMps) || Lane.SpeedLimitMps <= 0
			|| Lane.SpeedLimitMps > 55.6 || (Lane.SignalGroupId && !GroupControllers.Contains(Lane.SignalGroupId))
			|| Lane.bTerminal != Lane.Successors.IsEmpty())
		{
			return Fail(FString::Printf(TEXT("Invalid traffic lane %u metadata."), Lane.Id));
		}
		Lanes.Add(Lane.Id, &Lane);
		PointCount += Lane.PointsEnuM.Num();
		if (PointCount > 16384) { return Fail(TEXT("Traffic point budget exceeded.")); }
		for (int32 Index = 0; Index < Lane.PointsEnuM.Num(); ++Index)
		{
			if (!IsFinite(Lane.PointsEnuM[Index])) { return Fail(TEXT("Non-finite traffic point.")); }
			if (!Index) { continue; }
			const FVector A = Lane.PointsEnuM[Index-1];
			const FVector B = Lane.PointsEnuM[Index];
			const FVector Delta = B-A;
			const double Length = Delta.Size();
			const FVector Right(Delta.Y, -Delta.X, 0.0);
			if (Length < 0.05 || Length > 100 || Right.IsNearlyZero())
			{
				return Fail(TEXT("Traffic lane contains a degenerate segment."));
			}
			const FVector UnitRight = Right.GetSafeNormal();
			const int32 Samples = FMath::Max(1, FMath::CeilToInt(Length));
			GroundSamples += (Samples + 1) * 3;
			if (GroundSamples > 100000) { return Fail(TEXT("Traffic ground-query budget exceeded.")); }
			for (int32 Sample = 0; Sample <= Samples; ++Sample)
			{
				const FVector Center = FMath::Lerp(A, B, static_cast<double>(Sample) / Samples);
				for (double Side : {-0.5, 0.0, 0.5})
				{
					const FVector Test = Center + UnitRight * (Side * Lane.WidthM);
					double Height = 0.0;
					if (!GroundTop(Source, Test, true, Height) || FMath::Abs(Height-Center.Z) > 0.08)
					{
						return Fail(FString::Printf(TEXT("Lane %u leaves authored asphalt at %.3f,%.3f."),
							Lane.Id, Test.X, Test.Y));
					}
				}
			}
		}
	}

	TSet<uint32> SignalIds;
	int32 VehicleHeadCount = 0;
	int32 PedestrianHeadCount = 0;
	for (const FTrafficSignal& Signal : Layout.Signals)
	{
		const uint32* Owner = GroupControllers.Find(Signal.GroupId);
		if (!Signal.Id || SignalIds.Contains(Signal.Id) || !Owner || *Owner != Signal.ControllerId
			|| !IsFinite(Signal.PositionEnuM) || !FMath::IsFinite(Signal.HeadingDegrees)
			|| Signal.HeadingDegrees < 0 || Signal.HeadingDegrees >= 360)
		{
			return Fail(TEXT("Invalid traffic signal identity/controller/group/pose."));
		}
		SignalIds.Add(Signal.Id);
		Signal.Kind == ETrafficSignalKind::Vehicle ? ++VehicleHeadCount : ++PedestrianHeadCount;
	}
	if (VehicleHeadCount != 8 || PedestrianHeadCount != 8)
	{
		return Fail(TEXT("SignalCity requires exactly 8 vehicle and 8 pedestrian heads."));
	}
	for (const FTrafficLane& Lane : Layout.Lanes)
	{
		if (Lane.LaneChanges.Num() > 2) { return Fail(TEXT("Too many lane-change neighbors.")); }
		TSet<uint32> Successors;
		for (uint32 Id : Lane.Successors)
		{
			const FTrafficLane* const* Next = Lanes.Find(Id);
			if (!Next || Id == Lane.Id || Successors.Contains(Id)
				|| FVector::Dist(Lane.PointsEnuM.Last(), (*Next)->PointsEnuM[0]) > 0.15
				|| (Lane.SignalGroupId && (*Next)->SignalGroupId))
			{
				return Fail(FString::Printf(TEXT("Lane %u has invalid successor %u."), Lane.Id, Id));
			}
			Successors.Add(Id);
		}
		TSet<uint32> ChangeTargets;
		for (const FTrafficLaneChange& Change : Lane.LaneChanges)
		{
			const FTrafficLane* const* Found = Lanes.Find(Change.TargetLaneId);
			if (!Found || Change.TargetLaneId == Lane.Id || ChangeTargets.Contains(Change.TargetLaneId))
			{
				return Fail(TEXT("Invalid or duplicate lane-change neighbor."));
			}
			ChangeTargets.Add(Change.TargetLaneId);
			const FTrafficLane& Target = **Found;
			const FTrafficLaneChange* Reciprocal = Target.LaneChanges.FindByPredicate(
				[&](const auto& Other) { return Other.TargetLaneId == Lane.Id; });
			const double Span = Change.SourceEndM - Change.SourceBeginM;
			const double TargetSpan = Change.TargetEndM - Change.TargetBeginM;
			if (!FMath::IsFinite(Change.SourceBeginM) || !FMath::IsFinite(Change.SourceEndM)
				|| !FMath::IsFinite(Change.TargetBeginM) || !FMath::IsFinite(Change.TargetEndM)
				|| !Reciprocal || FMath::Abs(Change.SourceBeginM - Reciprocal->TargetBeginM) > 1.e-4
				|| FMath::Abs(Change.SourceEndM - Reciprocal->TargetEndM) > 1.e-4
				|| FMath::Abs(Change.TargetBeginM - Reciprocal->SourceBeginM) > 1.e-4
				|| FMath::Abs(Change.TargetEndM - Reciprocal->SourceEndM) > 1.e-4
				|| Change.SourceBeginM < 5.0 || Change.TargetBeginM < 5.0 || Span < 8 || TargetSpan < 8
				|| Change.SourceEndM > ArcLength(Lane.PointsEnuM)-5.0
				|| Change.TargetEndM > ArcLength(Target.PointsEnuM)-5.0
				|| FMath::Abs(Span-TargetSpan) > 0.5 || Lane.SignalGroupId != Target.SignalGroupId)
			{
				return Fail(TEXT("Lane-change windows must be reciprocal and clear of junctions."));
			}
			const int32 Steps = FMath::CeilToInt(Span);
			double SideSign = 0.0;
			GroundSamples += (Steps + 1) * 5;
			if (GroundSamples > 100000) { return Fail(TEXT("Traffic ground-query budget exceeded.")); }
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double T = static_cast<double>(Step) / Steps;
				FVector Tangent, OtherTangent;
				const FVector A = SampleArc(Lane, Change.SourceBeginM + Span*T, &Tangent);
				const FVector B = SampleArc(Target, Change.TargetBeginM + TargetSpan*T, &OtherTangent);
				const FVector Across = B-A;
				const double Side = Tangent.X*Across.Y - Tangent.Y*Across.X;
				const double Distance = Across.Size2D();
				const double Expected = (Lane.WidthM + Target.WidthM) * 0.5;
				if (FVector::DotProduct(Tangent, OtherTangent) < 0.9848
					|| FMath::Abs(FVector::DotProduct(Across, Tangent)) > 0.5
					|| FMath::Abs(Across.Z) > 0.1 || Distance < Expected-0.1
					|| Distance > Expected+0.75 || (SideSign != 0 && SideSign*Side <= 0))
				{
					return Fail(TEXT("Lane-change windows must be adjacent, parallel and same-direction."));
				}
				SideSign = Side;
				const FVector Right(Tangent.Y, -Tangent.X, 0.0);
				const double Width = Distance + FMath::Max(Lane.WidthM, Target.WidthM);
				for (double Fraction : {-0.5, -0.25, 0.0, 0.25, 0.5})
				{
					const FVector Test = (A+B)*0.5 + Right*(Width*Fraction);
					double Height;
					if (!GroundTop(Source, Test, true, Height) || FMath::Abs(Height-Test.Z) > 0.08)
					{
						return Fail(TEXT("Lane-change corridor leaves authored asphalt."));
					}
				}
			}
		}
		if (Lane.SignalGroupId)
		{
			const bool bHasHead = Layout.Signals.ContainsByPredicate([&](const FTrafficSignal& Signal)
			{
				return Signal.Kind == ETrafficSignalKind::Vehicle
					&& Signal.GroupId == Lane.SignalGroupId
					&& FVector::Dist(Signal.PositionEnuM, Lane.PointsEnuM.Last()) <= 15;
			});
			if (!bHasHead) { return Fail(TEXT("Controlled stop line has no nearby signal head.")); }
		}
	}
	for (const FTrafficSignal& Signal : Layout.Signals)
	{
		if (Signal.Kind == ETrafficSignalKind::Pedestrian) { continue; }
		const bool bHasStopline = Layout.Lanes.ContainsByPredicate([&](const FTrafficLane& Lane)
		{
			return Lane.SignalGroupId == Signal.GroupId
				&& FVector::Dist(Signal.PositionEnuM, Lane.PointsEnuM.Last()) <= 15;
		});
		if (!bHasStopline) { return Fail(TEXT("Traffic signal has no matching nearby stop line.")); }
	}
	return true;
}

bool SerializeTrafficLayout(const FTrafficLayout& Layout, const FString& SourceMapChecksum,
	FString& OutJson, FString& OutError)
{
	OutJson.Reset();
	if (!ValidateTrafficLayout(Layout, OutError)) { return false; }
	if (!CheckChecksum(SourceMapChecksum))
	{
		OutError = TEXT("A canonical verified MapPackage FNV-1a64 checksum is required.");
		return false;
	}
	FTrafficLayout Sorted = Layout;
	Sorted.Lanes.Sort([](const auto& A, const auto& B) { return A.Id < B.Id; });
	Sorted.Signals.Sort([](const auto& A, const auto& B) { return A.Id < B.Id; });
	Sorted.SignalPlans.Sort([](const auto& A, const auto& B) { return A.Id < B.Id; });
	using FWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	const TSharedRef<FWriter> Writer = TJsonWriterFactory<TCHAR,
		TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	auto Vector = [&Writer](const FVector& Value)
	{
		Writer->WriteArrayStart();
		for (double Number : {Value.X, Value.Y, Value.Z})
		{
			Writer->WriteValue(static_cast<double>(FMath::RoundToInt64(Number * 1.e6)) / 1.e6);
		}
		Writer->WriteArrayEnd();
	};
	auto SortedIds = [&Writer](const FString& Name, TArray<uint32> Values)
	{
		Values.Sort();
		Writer->WriteArrayStart(Name);
		for (uint32 Value : Values) { Writer->WriteValue(Value); }
		Writer->WriteArrayEnd();
	};
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("format_version"), 2);
	Writer->WriteValue(TEXT("source_map_checksum"), SourceMapChecksum);
	Writer->WriteArrayStart(TEXT("lanes"));
	for (FTrafficLane& Lane : Sorted.Lanes)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Lane.Id);
		Writer->WriteValue(TEXT("width_m"), Lane.WidthM);
		Writer->WriteValue(TEXT("speed_limit_mps"), Lane.SpeedLimitMps);
		Writer->WriteValue(TEXT("signal_group_id"), Lane.SignalGroupId);
		Writer->WriteValue(TEXT("terminal"), Lane.bTerminal);
		Writer->WriteArrayStart(TEXT("points"));
		for (const FVector& Position : Lane.PointsEnuM) { Vector(Position); }
		Writer->WriteArrayEnd();
		SortedIds(TEXT("successors"), Lane.Successors);
		if (!Lane.LaneChanges.IsEmpty())
		{
			Lane.LaneChanges.Sort([](const auto& A, const auto& B) { return A.TargetLaneId < B.TargetLaneId; });
			Writer->WriteArrayStart(TEXT("lane_changes"));
			for (const FTrafficLaneChange& Change : Lane.LaneChanges)
			{
				Writer->WriteObjectStart();
				Writer->WriteValue(TEXT("target_lane_id"), Change.TargetLaneId);
				Writer->WriteValue(TEXT("source_begin_m"), Change.SourceBeginM);
				Writer->WriteValue(TEXT("source_end_m"), Change.SourceEndM);
				Writer->WriteValue(TEXT("target_begin_m"), Change.TargetBeginM);
				Writer->WriteValue(TEXT("target_end_m"), Change.TargetEndM);
				Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd();
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteArrayStart(TEXT("signals"));
	for (const FTrafficSignal& Signal : Sorted.Signals)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Signal.Id);
		Writer->WriteValue(TEXT("controller_id"), Signal.ControllerId);
		Writer->WriteValue(TEXT("group_id"), Signal.GroupId);
		Writer->WriteValue(TEXT("kind"), Signal.Kind == ETrafficSignalKind::Pedestrian
			? TEXT("pedestrian") : TEXT("vehicle"));
		Writer->WriteIdentifierPrefix(TEXT("position_enu")); Vector(Signal.PositionEnuM);
		Writer->WriteValue(TEXT("heading_deg"), Signal.HeadingDegrees);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteArrayStart(TEXT("signal_plans"));
	for (FSignalPlan& Plan : Sorted.SignalPlans)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Plan.Id);
		Writer->WriteValue(TEXT("offset_ms"), Plan.OffsetMs);
		SortedIds(TEXT("groups"), Plan.Groups);
		Writer->WriteArrayStart(TEXT("phases"));
		for (FSignalPhase& Phase : Plan.Phases)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("duration_ms"), Phase.DurationMs);
			SortedIds(TEXT("green_groups"), Phase.GreenGroups);
			SortedIds(TEXT("yellow_groups"), Phase.YellowGroups);
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	if (!Writer->Close())
	{
		OutError = TEXT("JSON writer failed.");
		OutJson.Reset();
		return false;
	}
	OutJson += TEXT("\n");
	return true;
}
}
