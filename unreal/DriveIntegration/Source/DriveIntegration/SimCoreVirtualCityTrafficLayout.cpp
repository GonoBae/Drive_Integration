#include "SimCoreVirtualCityTrafficLayout.h"

#include "SimCoreVirtualCityLayout.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

namespace SimCoreVirtualCity
{
namespace
{
	bool IsFinite(const FVector& Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z);
	}

	// Project to the SAME oriented top slabs used by BuildLayout, including the grade.
	// Export additionally raycasts the saved map; this analytic projection is not a bake.
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

	void Point(TArray<FVector>& Points, double East, double North)
	{
		const FVector Value(East, North, 0.0);
		if (Points.IsEmpty() || !Points.Last().Equals(Value, 1.e-7)) { Points.Add(Value); }
	}

	void Straight(TArray<FVector>& Points, FVector2D Start, FVector2D End)
	{
		const int32 Count = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Start, End)));
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			const FVector2D Position = FMath::Lerp(Start, End, static_cast<double>(Index) / Count);
			Point(Points, Position.X, Position.Y);
		}
	}

	void Arc(TArray<FVector>& Points, FVector2D Center, double Radius, double Start, double End)
	{
		const int32 Count = FMath::Max(18, FMath::CeilToInt(FMath::Abs(End - Start) * PI / 180.0 * Radius));
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			const double Angle = FMath::DegreesToRadians(FMath::Lerp(Start, End, static_cast<double>(Index) / Count));
			Point(Points, Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle));
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

	void AddArc(FTrafficLayout& Layout, uint32 Id, FVector2D Center, double Radius,
		double Start, double End, uint32 Successor)
	{
		Arc(Add(Layout, Id, {Successor}, 0, 5.0).PointsEnuM, Center, Radius, Start, End);
	}

	void RightTurnToBranch(TArray<FVector>& Points)
	{
		// The existing T has square road edges. A slight in-lane offset keeps the
		// entire 3m connector corridor on asphalt, clear of the inside corner curb.
		const FVector2D A(8.0, 144.0), B(7.4, 144.0), C(6.6, 143.4), D(6.0, 143.4);
		for (int32 Index = 0; Index <= 10; ++Index)
		{
			const double T = Index / 10.0;
			const double S = 1.0 - T;
			const FVector2D P = A * (S*S*S) + B * (3*S*S*T) + C * (3*S*T*T) + D * (T*T*T);
			Point(Points, P.X, P.Y);
		}
		Arc(Points, FVector2D(6.0, 147.4), 4.0, -90.0, -180.0);
		Straight(Points, FVector2D(2.0, 147.4), FVector2D(2.0, 152.0));
	}
}

FTrafficLayout BuildTrafficLayout()
{
	FTrafficLayout Result;
	Result.Lanes.Reserve(25);
	// Clockwise inner lane: north EB, east SB, south WB, west NB (radius 14m).
	AddStraight(Result, 100, {-76,140}, {-8,140}, {110}, 1);
	AddStraight(Result, 110, {-8,140}, {8,140}, {120});
	AddStraight(Result, 120, {8,140}, {76,140}, {130});
	AddArc(Result, 130, {76,126}, 14, 90, 0, 140);
	AddStraight(Result, 140, {90,126}, {90,18}, {150});
	AddArc(Result, 150, {76,18}, 14, 0, -90, 160);
	AddStraight(Result, 160, {76,4}, {-76,4}, {170});
	AddArc(Result, 170, {-76,18}, 14, -90, -180, 180);
	AddStraight(Result, 180, {-90,18}, {-90,126}, {190});
	AddArc(Result, 190, {-76,126}, 14, 180, 90, 100);
	// Counterclockwise outer lane: the opposite direction, not the QA route.
	AddStraight(Result, 200, {76,144}, {8,144}, {210,300}, 1);
	AddStraight(Result, 210, {8,144}, {-8,144}, {220});
	AddStraight(Result, 220, {-8,144}, {-76,144}, {230});
	AddArc(Result, 230, {-76,126}, 18, 90, 180, 240);
	AddStraight(Result, 240, {-94,126}, {-94,18}, {250});
	AddArc(Result, 250, {-76,18}, 18, 180, 270, 260);
	AddStraight(Result, 260, {-76,0}, {76,0}, {270});
	AddArc(Result, 270, {76,18}, 18, 270, 360, 280);
	AddStraight(Result, 280, {94,18}, {94,126}, {290});
	AddArc(Result, 290, {76,126}, 18, 0, 90, 200);
	TArray<FVector> MainRight;
	RightTurnToBranch(MainRight);
	Add(Result, 300, {340}, 0, 2.5).PointsEnuM = MainRight;
	AddStraight(Result, 310, {-2,168}, {-2,152}, {320,330}, 2, 4.166667);
	// Mirroring and reversing the right turn preserves both endpoint tangents.
	TArray<FVector>& BranchRight = Add(Result, 320, {220}, 0, 2.5).PointsEnuM;
	for (int32 Index = MainRight.Num() - 1; Index >= 0; --Index)
	{
		Point(BranchRight, -MainRight[Index].X, MainRight[Index].Y);
	}
	TArray<FVector>& BranchLeft = Add(Result, 330, {120}, 0, 3.0).PointsEnuM;
	Straight(BranchLeft, {-2,152}, {-2,150});
	Arc(BranchLeft, {8,150}, 10, 180, 270);
	// Terminal and incoming source are distinct. No physically impossible bay U-turn.
	AddStraight(Result, 340, {2,152}, {2,168}, {}, 0, 4.166667);
	Result.Signals = {
		{1, 1, FVector(-8,137,0), 90},
		{2, 1, FVector(8,147,0), 270},
		{3, 2, FVector(-5,152,0), 180}
	};
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
	if (Layout.Lanes.IsEmpty() || Layout.Lanes.Num() > 256 || Layout.Signals.IsEmpty() || Layout.Signals.Num() > 32)
	{
		return Fail(TEXT("Traffic lane/signal count is outside the bounded contract."));
	}
	TMap<uint32, const FTrafficLane*> Lanes;
	int32 PointCount = 0;
	int32 GroundSampleCount = 0;
	const FLayout Source = BuildLayout();
	for (const FTrafficLane& Lane : Layout.Lanes)
	{
		if (!Lane.Id || Lanes.Contains(Lane.Id) || Lane.PointsEnuM.Num() < 2 || Lane.PointsEnuM.Num() > 2048
			|| !FMath::IsFinite(Lane.WidthM) || Lane.WidthM < 2 || Lane.WidthM > 8
			|| !FMath::IsFinite(Lane.SpeedLimitMps) || Lane.SpeedLimitMps <= 0 || Lane.SpeedLimitMps > 55.6
			|| Lane.SignalGroupId > 2 || Lane.bTerminal != Lane.Successors.IsEmpty())
		{
			return Fail(FString::Printf(TEXT("Invalid traffic lane %u metadata/terminal declaration."), Lane.Id));
		}
		Lanes.Add(Lane.Id, &Lane);
		PointCount += Lane.PointsEnuM.Num();
		if (PointCount > 16384) { return Fail(TEXT("Traffic point budget exceeded.")); }
		for (int32 Index = 0; Index < Lane.PointsEnuM.Num(); ++Index)
		{
			if (!IsFinite(Lane.PointsEnuM[Index])) { return Fail(TEXT("Non-finite traffic point.")); }
			if (!Index) { continue; }
			const FVector A = Lane.PointsEnuM[Index-1], B = Lane.PointsEnuM[Index];
			const double Length = FVector::Dist(A, B);
			if (Length < 0.05 || Length > 100) { return Fail(TEXT("Invalid traffic segment length.")); }
			const FVector Delta = B-A;
			const FVector Right = FVector(Delta.Y, -Delta.X, 0).GetSafeNormal();
			if (Right.IsNearlyZero()) { return Fail(TEXT("Traffic segment has no horizontal direction.")); }
			const int32 Samples = FMath::Max(1, FMath::CeilToInt(Length));
			GroundSampleCount += (Samples + 1) * 3;
			if (GroundSampleCount > 100000) { return Fail(TEXT("Traffic ground-sample budget exceeded.")); }
			for (int32 Sample = 0; Sample <= Samples; ++Sample)
			{
				const FVector Center = FMath::Lerp(A, B, static_cast<double>(Sample)/Samples);
				for (double Side : {-0.5, 0.0, 0.5})
				{
					const FVector Test = Center + Right * (Side * Lane.WidthM);
					double Height = 0;
					if (!GroundTop(Source, Test, true, Height) || FMath::Abs(Height-Center.Z) > 0.08)
					{
						return Fail(FString::Printf(TEXT("Lane %u corridor leaves authored asphalt at %.3f,%.3f."), Lane.Id, Test.X, Test.Y));
					}
				}
			}
		}
	}
	TSet<uint32> SignalIds;
	for (const FTrafficSignal& Signal : Layout.Signals)
	{
		if (!Signal.Id || SignalIds.Contains(Signal.Id) || (Signal.GroupId != 1 && Signal.GroupId != 2)
			|| !IsFinite(Signal.PositionEnuM) || !FMath::IsFinite(Signal.HeadingDegrees)
			|| Signal.HeadingDegrees < 0 || Signal.HeadingDegrees >= 360)
		{
			return Fail(TEXT("Invalid traffic signal identity/group/pose."));
		}
		SignalIds.Add(Signal.Id);
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
				return Fail(FString::Printf(TEXT("Lane %u has missing/discontinuous/invalid successor %u."), Lane.Id, Id));
			}
			Successors.Add(Id);
		}
		if (Lane.SignalGroupId)
		{
			bool bHasHead = false;
			for (const FTrafficSignal& Signal : Layout.Signals)
			{
				bHasHead |= Signal.GroupId == Lane.SignalGroupId
					&& FVector::Dist(Signal.PositionEnuM, Lane.PointsEnuM.Last()) <= 15;
			}
			if (!bHasHead) { return Fail(TEXT("Controlled stop line has no nearby signal head.")); }
		}
	}
	return true;
}

bool SerializeTrafficLayout(const FTrafficLayout& Layout, const FString& SourceMapChecksum,
	FString& OutJson, FString& OutError)
{
	OutJson.Reset();
	if (!ValidateTrafficLayout(Layout, OutError)) { return false; }
	if (SourceMapChecksum.Len() != 24 || !SourceMapChecksum.StartsWith(TEXT("fnv1a64:")))
	{
		OutError = TEXT("A verified MapPackage FNV-1a64 checksum is required."); return false;
	}
	for (int32 Index = 8; Index < SourceMapChecksum.Len(); ++Index)
	{
		const TCHAR C = SourceMapChecksum[Index];
		if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f')))
		{
			OutError = TEXT("Checksum must use canonical lowercase hexadecimal."); return false;
		}
	}
	FTrafficLayout Sorted = Layout;
	Sorted.Lanes.Sort([](const FTrafficLane& A, const FTrafficLane& B) { return A.Id < B.Id; });
	Sorted.Signals.Sort([](const FTrafficSignal& A, const FTrafficSignal& B) { return A.Id < B.Id; });
	using FWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	const TSharedRef<FWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	auto Vector = [&Writer](const FVector& Value)
	{
		Writer->WriteArrayStart();
		for (double Number : {Value.X, Value.Y, Value.Z})
		{
			// Sub-micrometer precision is irrelevant to the meter-based package;
			// quantization also avoids -0 and raycast/analytic roundoff differences.
			Writer->WriteValue(static_cast<double>(FMath::RoundToInt64(Number * 1.e6)) / 1.e6);
		}
		Writer->WriteArrayEnd();
	};
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("format_version"), 1);
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
		Lane.Successors.Sort();
		Writer->WriteArrayStart(TEXT("successors"));
		for (uint32 Id : Lane.Successors) { Writer->WriteValue(Id); }
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteArrayStart(TEXT("signals"));
	for (const FTrafficSignal& Signal : Sorted.Signals)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Signal.Id);
		Writer->WriteValue(TEXT("group_id"), Signal.GroupId);
		Writer->WriteIdentifierPrefix(TEXT("position_enu")); Vector(Signal.PositionEnuM);
		Writer->WriteValue(TEXT("heading_deg"), Signal.HeadingDegrees);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	if (!Writer->Close()) { OutError = TEXT("JSON writer failed."); OutJson.Reset(); return false; }
	OutJson += TEXT("\n");
	return true;
}
}
