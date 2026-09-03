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
		const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D,
		bool bReverse, const FVector2D& ExactStart, const FVector2D& ExactEnd,
		uint32 Successor)
	{
		TArray<FVector> Center;
		Cubic(Center, A, B, C, D, 48);
		if (bReverse) { Algo::Reverse(Center); }
		TArray<FVector>& Points = Add(Layout, Id, {Successor}, 0, 6.944444).PointsEnuM;
		Point(Points, ExactStart);
		for (int32 Index = 0; Index < Center.Num(); ++Index)
		{
			const FVector Previous = Center[FMath::Max(0, Index-1)];
			const FVector Next = Center[FMath::Min(Center.Num()-1, Index+1)];
			const FVector2D Tangent(Next.X-Previous.X, Next.Y-Previous.Y);
			const FVector2D Right(Tangent.Y, -Tangent.X);
			const FVector2D Position(Center[Index].X, Center[Index].Y);
			Point(Points, Position + Right.GetSafeNormal() * 2.0);
		}
		Point(Points, ExactEnd);
	}

	void AddMovement(FTrafficLayout& Layout, uint32 Id, FVector2D A, FVector2D B,
		FVector2D C, FVector2D D, uint32 Successor)
	{
		Cubic(Add(Layout, Id, {Successor}, 0, 4.166667).PointsEnuM, A, B, C, D, 16);
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
	Result.Lanes.Reserve(44);
	Result.Signals.Reserve(16);

	// South intersection, east/west approaches and all permissive movements.
	AddStraight(Result, 1010, {-105,38}, {-10,38}, {1011,1012,1013}, 101);
	AddStraight(Result, 1011, {-10,38}, {10,38}, {1014}, 0, 6.944444);
	AddMovement(Result, 1012, {-10,38}, {-5,38}, {-2,35}, {-2,30}, 1044);
	AddMovement(Result, 1013, {-10,38}, {0,38}, {2,40}, {2,50}, 1034);
	AddStraight(Result, 1014, {10,38}, {105,38}, {3001});

	AddStraight(Result, 1020, {105,42}, {10,42}, {1021,1022,1023}, 101);
	AddStraight(Result, 1021, {10,42}, {-10,42}, {1024}, 0, 6.944444);
	AddMovement(Result, 1022, {10,42}, {5,42}, {2,45}, {2,50}, 1034);
	AddMovement(Result, 1023, {10,42}, {0,42}, {-2,40}, {-2,30}, 1044);
	AddStraight(Result, 1024, {-10,42}, {-105,42}, {3002});

	// Central avenue. The middle approaches are controlled by the other junction.
	AddStraight(Result, 1030, {2,-10}, {2,30}, {1031,1032,1033}, 102);
	AddStraight(Result, 1031, {2,30}, {2,50}, {1034}, 0, 6.944444);
	AddMovement(Result, 1032, {2,30}, {2,35}, {5,38}, {10,38}, 1014);
	AddMovement(Result, 1033, {2,30}, {2,40}, {0,42}, {-10,42}, 1024);
	AddStraight(Result, 1034, {2,50}, {2,110}, {2031,2032,2033}, 202);

	AddStraight(Result, 2040, {-2,168}, {-2,130}, {2041,2042,2043}, 202);
	AddStraight(Result, 2041, {-2,130}, {-2,110}, {1040}, 0, 6.944444);
	AddMovement(Result, 2042, {-2,130}, {-2,125}, {-5,122}, {-10,122}, 2024);
	AddMovement(Result, 2043, {-2,130}, {-2,120}, {0,118}, {10,118}, 2014);
	AddStraight(Result, 1040, {-2,110}, {-2,50}, {1041,1042,1043}, 102);
	AddStraight(Result, 1041, {-2,50}, {-2,30}, {1044}, 0, 6.944444);
	AddMovement(Result, 1042, {-2,50}, {-2,45}, {-5,42}, {-10,42}, 1024);
	AddMovement(Result, 1043, {-2,50}, {-2,40}, {0,38}, {10,38}, 1014);
	AddStraight(Result, 1044, {-2,30}, {-2,-10}, {});

	// North intersection, again with explicit straight/right/left connectors.
	AddStraight(Result, 2010, {-90,118}, {-10,118}, {2011,2012,2013}, 201);
	AddStraight(Result, 2011, {-10,118}, {10,118}, {2014}, 0, 6.944444);
	AddMovement(Result, 2012, {-10,118}, {-5,118}, {-2,115}, {-2,110}, 1040);
	AddMovement(Result, 2013, {-10,118}, {0,118}, {2,120}, {2,130}, 2034);
	AddStraight(Result, 2014, {10,118}, {100,118}, {3003});

	AddStraight(Result, 2020, {100,122}, {10,122}, {2021,2022,2023}, 201);
	AddStraight(Result, 2021, {10,122}, {-10,122}, {2024}, 0, 6.944444);
	AddMovement(Result, 2022, {10,122}, {5,122}, {2,125}, {2,130}, 2034);
	AddMovement(Result, 2023, {10,122}, {0,122}, {-2,120}, {-2,110}, 1040);
	AddStraight(Result, 2024, {-10,122}, {-90,122}, {3004});

	AddStraight(Result, 2031, {2,110}, {2,130}, {2034}, 0, 6.944444);
	AddMovement(Result, 2032, {2,110}, {2,115}, {5,118}, {10,118}, 2014);
	AddMovement(Result, 2033, {2,110}, {2,120}, {0,122}, {-10,122}, 2024);
	AddStraight(Result, 2034, {2,130}, {2,168}, {});

	// Two asymmetric, bidirectional collectors close the urban blocks without
	// falling back to the old map's rectangular perimeter road.
	const FVector2D EastA(105,40), EastB(112,68), EastC(110,101), EastD(100,120);
	const FVector2D WestA(-105,40), WestB(-112,62), WestC(-108,101), WestD(-90,120);
	AddCollector(Result, 3001, EastA, EastB, EastC, EastD, false, {105,38}, {100,122}, 2020);
	AddCollector(Result, 3002, WestA, WestB, WestC, WestD, false, {-105,42}, {-90,118}, 2010);
	AddCollector(Result, 3003, EastA, EastB, EastC, EastD, true, {100,118}, {105,42}, 1020);
	AddCollector(Result, 3004, WestA, WestB, WestC, WestD, true, {-90,122}, {-105,38}, 1010);

	Result.Signals = {
		{1, 1, 101, FVector(-8,35,0), 90},
		{2, 1, 101, FVector(8,45,0), 270},
		{3, 1, 102, FVector(5,32,0), 0},
		{4, 1, 102, FVector(-5,48,0), 180},
		{5, 2, 201, FVector(-8,115,0), 90},
		{6, 2, 201, FVector(8,125,0), 270},
		{7, 2, 202, FVector(5,112,0), 0},
		{8, 2, 202, FVector(-5,128,0), 180},
		// Two opposing pedestrian heads define each crosswalk segment. The
		// shared vehicle group is the parallel, non-conflicting WALK authority.
		{101, 1, 101, FVector(-6,34,0), 90, ETrafficSignalKind::Pedestrian},
		{102, 1, 101, FVector(6,34,0), 270, ETrafficSignalKind::Pedestrian},
		{103, 1, 102, FVector(6,35,0), 0, ETrafficSignalKind::Pedestrian},
		{104, 1, 102, FVector(6,45,0), 180, ETrafficSignalKind::Pedestrian},
		{105, 2, 201, FVector(-6,114,0), 90, ETrafficSignalKind::Pedestrian},
		{106, 2, 201, FVector(6,114,0), 270, ETrafficSignalKind::Pedestrian},
		{107, 2, 202, FVector(6,115,0), 0, ETrafficSignalKind::Pedestrian},
		{108, 2, 202, FVector(6,125,0), 180, ETrafficSignalKind::Pedestrian},
	};

	const auto MakePlan = [](uint32 Id, uint32 Offset, uint32 EastWest, uint32 NorthSouth)
	{
		FSignalPlan Plan;
		Plan.Id = Id;
		Plan.OffsetMs = Offset;
		Plan.Groups = {EastWest, NorthSouth};
		Plan.Phases = {
			{2000, {}, {}},
			{13000, {EastWest}, {}},
			{3000, {}, {EastWest}},
			{2000, {}, {}},
			{11000, {NorthSouth}, {}},
			{3000, {}, {NorthSouth}},
			{2000, {}, {}},
		};
		return Plan;
	};
	Result.SignalPlans = {MakePlan(1, 0, 101, 102), MakePlan(2, 9000, 201, 202)};

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
