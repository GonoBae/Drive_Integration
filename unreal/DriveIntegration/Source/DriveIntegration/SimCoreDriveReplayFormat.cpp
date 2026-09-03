#include "SimCoreDriveReplay.h"

#include "Algo/BinarySearch.h"

namespace
{
	bool Finite(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool SafeIdentity(const FString& Value)
	{
		return !Value.IsEmpty() && Value.Len() <= 256
			&& !Value.Contains(TEXT(",")) && !Value.Contains(TEXT("\n"))
			&& !Value.Contains(TEXT("\r"));
	}

	float LerpHeading(float A, float B, double Alpha)
	{
		return FMath::Fmod(A + FMath::FindDeltaAngleDegrees(A, B) * Alpha + 360.0f, 360.0f);
	}

	bool ValidFrame(const SimCoreDriveReplay::FFrame& Frame)
	{
		return Frame.Sequence > 0 && Finite(Frame.PositionEnu) && Finite(Frame.LinearVelocityEnu)
			&& FMath::IsFinite(Frame.HeadingDegrees) && Frame.HeadingDegrees >= 0.0f
			&& Frame.HeadingDegrees < 360.0f && FMath::IsFinite(Frame.PitchDegrees)
			&& FMath::IsFinite(Frame.RollDegrees) && FMath::IsFinite(Frame.SpeedMps)
			&& FMath::IsFinite(Frame.CollisionHalfLengthMeters)
			&& FMath::IsFinite(Frame.CollisionHalfWidthMeters)
			&& FMath::IsFinite(Frame.CollisionHalfHeightMeters)
			&& Frame.CollisionHalfLengthMeters > 0.0f
			&& Frame.CollisionHalfWidthMeters > 0.0f
			&& Frame.CollisionHalfHeightMeters > 0.0f;
	}
}

void SimCoreDriveReplay::FTrack::Reset()
{
	MapChecksum.Reset();
	PlaySessionId.Reset();
	Frames.Reset();
}

bool SimCoreDriveReplay::FTrack::Capture(const SimCoreProtocol::FVehicleState& State)
{
	if (!SimCoreProtocol::IsValidTrafficNetworkChecksum(State.MapPackageChecksum)
		|| !SafeIdentity(State.PlaySessionId) || State.Sequence == 0
		|| Frames.Num() >= MaxFrames)
	{
		return false;
	}
	if (Frames.IsEmpty())
	{
		MapChecksum = State.MapPackageChecksum;
		PlaySessionId = State.PlaySessionId;
	}
	else if (MapChecksum != State.MapPackageChecksum || PlaySessionId != State.PlaySessionId
		|| State.Sequence <= Frames.Last().Sequence
		|| State.SimulationTimeNs <= Frames.Last().SimulationTimeNs)
	{
		return false;
	}
	FFrame Frame;
	Frame.SimulationTimeNs = State.SimulationTimeNs;
	Frame.Sequence = State.Sequence;
	Frame.PositionEnu = State.PositionEnu;
	Frame.LinearVelocityEnu = State.LinearVelocityEnu;
	Frame.HeadingDegrees = State.HeadingDegrees;
	Frame.PitchDegrees = State.PitchDegrees;
	Frame.RollDegrees = State.RollDegrees;
	Frame.SpeedMps = State.SpeedMps;
	Frame.CollisionHalfLengthMeters = State.CollisionHalfLengthMeters;
	Frame.CollisionHalfWidthMeters = State.CollisionHalfWidthMeters;
	Frame.CollisionHalfHeightMeters = State.CollisionHalfHeightMeters;
	if (!ValidFrame(Frame)) return false;
	Frames.Add(Frame);
	return true;
}

double SimCoreDriveReplay::FTrack::DurationSeconds() const
{
	return Frames.Num() < 2 ? 0.0
		: static_cast<double>(Frames.Last().SimulationTimeNs - Frames[0].SimulationTimeNs) / 1.e9;
}

bool SimCoreDriveReplay::Sample(
	const FTrack& Track, double ElapsedSeconds,
	SimCoreProtocol::FVehicleState& OutState)
{
	OutState = {};
	if (Track.Frames.IsEmpty() || !FMath::IsFinite(ElapsedSeconds) || ElapsedSeconds < 0.0
		|| !SafeIdentity(Track.MapChecksum) || !SafeIdentity(Track.PlaySessionId)) return false;
	const uint64 StartNs = Track.Frames[0].SimulationTimeNs;
	const double TargetNsDouble = static_cast<double>(StartNs) + ElapsedSeconds * 1.e9;
	const uint64 TargetNs = TargetNsDouble >= static_cast<double>(MAX_uint64)
		? MAX_uint64 : static_cast<uint64>(TargetNsDouble);
	int32 Upper = Algo::LowerBoundBy(Track.Frames, TargetNs,
		[](const FFrame& Frame) { return Frame.SimulationTimeNs; });
	Upper = FMath::Clamp(Upper, 0, Track.Frames.Num() - 1);
	const int32 Lower = FMath::Max(0, Upper - (Track.Frames[Upper].SimulationTimeNs > TargetNs ? 1 : 0));
	const FFrame& A = Track.Frames[Lower];
	const FFrame& B = Track.Frames[Upper];
	const double Denominator = static_cast<double>(B.SimulationTimeNs - A.SimulationTimeNs);
	const double Alpha = Denominator > 0.0
		? FMath::Clamp((static_cast<double>(TargetNs) - A.SimulationTimeNs) / Denominator, 0.0, 1.0)
		: 0.0;
	OutState.EntityId = 9001;
	OutState.EntityKind = SimCoreProtocol::EEntityKind::NpcVehicle;
	OutState.Sequence = Alpha < 0.5 ? A.Sequence : B.Sequence;
	OutState.SimulationTimeNs = TargetNs;
	OutState.MapPackageChecksum = Track.MapChecksum;
	OutState.PlaySessionId = Track.PlaySessionId;
	OutState.PositionEnu = FMath::Lerp(A.PositionEnu, B.PositionEnu, Alpha);
	OutState.LinearVelocityEnu = FMath::Lerp(A.LinearVelocityEnu, B.LinearVelocityEnu, Alpha);
	OutState.HeadingDegrees = LerpHeading(A.HeadingDegrees, B.HeadingDegrees, Alpha);
	OutState.PitchDegrees = FMath::Lerp(A.PitchDegrees, B.PitchDegrees, Alpha);
	OutState.RollDegrees = FMath::Lerp(A.RollDegrees, B.RollDegrees, Alpha);
	OutState.SpeedMps = FMath::Lerp(A.SpeedMps, B.SpeedMps, Alpha);
	OutState.CollisionHalfLengthMeters = FMath::Lerp(
		A.CollisionHalfLengthMeters, B.CollisionHalfLengthMeters, Alpha);
	OutState.CollisionHalfWidthMeters = FMath::Lerp(
		A.CollisionHalfWidthMeters, B.CollisionHalfWidthMeters, Alpha);
	OutState.CollisionHalfHeightMeters = FMath::Lerp(
		A.CollisionHalfHeightMeters, B.CollisionHalfHeightMeters, Alpha);
	return true;
}

FString SimCoreDriveReplay::SerializeCsv(const FTrack& Track)
{
	if (!SafeIdentity(Track.MapChecksum) || !SafeIdentity(Track.PlaySessionId)
		|| Track.Frames.IsEmpty()) return {};
	FString Result = FString::Printf(TEXT("%s,%s,%s\n"), FormatName,
		*Track.MapChecksum, *Track.PlaySessionId);
	for (const FFrame& Frame : Track.Frames)
	{
		if (!ValidFrame(Frame)) return {};
		Result += FString::Printf(TEXT("%llu,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"),
			Frame.SimulationTimeNs, Frame.Sequence,
			Frame.PositionEnu.X, Frame.PositionEnu.Y, Frame.PositionEnu.Z,
			Frame.LinearVelocityEnu.X, Frame.LinearVelocityEnu.Y, Frame.LinearVelocityEnu.Z,
			Frame.HeadingDegrees, Frame.PitchDegrees, Frame.RollDegrees, Frame.SpeedMps,
			Frame.CollisionHalfLengthMeters, Frame.CollisionHalfWidthMeters,
			Frame.CollisionHalfHeightMeters);
	}
	return Result;
}

bool SimCoreDriveReplay::ParseCsv(
	const FString& Csv, FTrack& OutTrack, FString& OutError)
{
	OutTrack.Reset();
	OutError.Reset();
	auto Fail = [&](const TCHAR* Error) { OutTrack.Reset(); OutError = Error; return false; };
	if (Csv.IsEmpty() || Csv.Len() > 16 * 1024 * 1024) return Fail(TEXT("Replay is empty or over 16 MiB"));
	// The writer emits one conventional terminal newline. Remove only that
	// terminator so an empty row inside the payload still fails closed.
	FString Normalized = Csv;
	if (Normalized.EndsWith(TEXT("\r\n"))) Normalized.LeftChopInline(2, EAllowShrinking::No);
	else if (Normalized.EndsWith(TEXT("\n"))) Normalized.LeftChopInline(1, EAllowShrinking::No);
	TArray<FString> Lines;
	Normalized.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() < 2 || Lines.Num() > MaxFrames + 1) return Fail(TEXT("Replay frame count is invalid"));
	TArray<FString> Header;
	Lines[0].ParseIntoArray(Header, TEXT(","), false);
	if (Header.Num() != 3 || Header[0] != FormatName
		|| !SimCoreProtocol::IsValidTrafficNetworkChecksum(Header[1])
		|| !SafeIdentity(Header[2])) return Fail(TEXT("Replay header is invalid"));
	OutTrack.MapChecksum = Header[1];
	OutTrack.PlaySessionId = Header[2];
	for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
	{
		TArray<FString> Fields;
		Lines[LineIndex].ParseIntoArray(Fields, TEXT(","), false);
		if (Fields.Num() != 15) return Fail(TEXT("Replay row field count is invalid"));
		FFrame Frame;
		double Values[13]{};
		if (!LexTryParseString(Frame.SimulationTimeNs, *Fields[0])
			|| !LexTryParseString(Frame.Sequence, *Fields[1])) return Fail(TEXT("Replay integer field is invalid"));
		for (int32 Index = 0; Index < 13; ++Index)
		{
			if (!LexTryParseString(Values[Index], *Fields[Index + 2]) || !FMath::IsFinite(Values[Index]))
				return Fail(TEXT("Replay numeric field is invalid"));
		}
		Frame.PositionEnu = {Values[0], Values[1], Values[2]};
		Frame.LinearVelocityEnu = {Values[3], Values[4], Values[5]};
		Frame.HeadingDegrees = Values[6]; Frame.PitchDegrees = Values[7];
		Frame.RollDegrees = Values[8]; Frame.SpeedMps = Values[9];
		Frame.CollisionHalfLengthMeters = Values[10];
		Frame.CollisionHalfWidthMeters = Values[11];
		Frame.CollisionHalfHeightMeters = Values[12];
		if (!ValidFrame(Frame)
			|| (!OutTrack.Frames.IsEmpty()
				&& (Frame.Sequence <= OutTrack.Frames.Last().Sequence
					|| Frame.SimulationTimeNs <= OutTrack.Frames.Last().SimulationTimeNs)))
			return Fail(TEXT("Replay rows are invalid or unordered"));
		OutTrack.Frames.Add(Frame);
	}
	return true;
}
