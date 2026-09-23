#include "SimCoreVehicleVisualProfile.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <filesystem>

namespace
{
using FObject = TSharedPtr<FJsonObject>;
constexpr int64 MaxFileBytes = 1024 * 1024;
const TCHAR* const ProfilePaths[] = {
	TEXT("profiles/sedan.json"), TEXT("profiles/compact.json"),
	TEXT("profiles/truck.json"), TEXT("profiles/motorcycle.json")};
const TCHAR* const VehicleIds[] = {
	TEXT("sedan_standard"), TEXT("compact_standard"), TEXT("truck_standard"), TEXT("motorcycle_standard")};

bool Fail(FString& Error, const FString& Reason)
{
	Error = Reason;
	return false;
}

bool InsideCatalog(const FString& Root, const FString& Path, FString& Error, FString* CanonicalKey = nullptr)
{
	namespace fs = std::filesystem;
	const auto Utf8Path = [](const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return fs::path(std::u8string(Utf8.Get(), Utf8.Get() + Utf8.Length()));
	};
	std::error_code Code;
	const auto CanonicalRoot = fs::canonical(Utf8Path(Root), Code);
	if (Code) return Fail(Error, Root + TEXT(": cannot resolve catalog directory"));
	const auto CanonicalFile = fs::canonical(Utf8Path(Path), Code);
	if (Code || !fs::is_regular_file(CanonicalFile, Code))
		return Fail(Error, Path + TEXT(": expected an existing regular file"));
	auto FilePart = CanonicalFile.begin();
	for (auto RootPart = CanonicalRoot.begin(); RootPart != CanonicalRoot.end(); ++RootPart, ++FilePart)
	{
		if (FilePart == CanonicalFile.end() || *FilePart != *RootPart)
			return Fail(Error, Path + TEXT(": file escapes catalog directory"));
	}
	if (FilePart == CanonicalFile.end()) return Fail(Error, Path + TEXT(": expected a file below catalog directory"));
	if (CanonicalKey)
	{
		const auto Name = CanonicalFile.generic_u8string();
		const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Name.data()), static_cast<int32>(Name.size()));
		*CanonicalKey = FString(Decoded.Length(), Decoded.Get()).ToLower();
	}
	return true;
}

bool ReadDocument(const FString& Path, TArray<uint8>& Bytes, FObject& Out, FString& Error)
{
	TUniquePtr<FArchive> Input(IFileManager::Get().CreateFileReader(*Path));
	if (!Input) return Fail(Error, Path + TEXT(": cannot read vehicle catalog file"));
	const int64 Size = Input->TotalSize();
	if (Size <= 0 || Size > MaxFileBytes) return Fail(Error, Path + TEXT(": file must be between 1 byte and 1 MiB"));
	Bytes.SetNumUninitialized(static_cast<int32>(Size));
	Input->Serialize(Bytes.GetData(), Size);
	if (Input->IsError()) return Fail(Error, Path + TEXT(": incomplete file read"));
	Input->Close();
	if (Bytes.Contains(0)) return Fail(Error, Path + TEXT(": embedded NUL in JSON"));
	const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	const FString Text(Decoded.Length(), Decoded.Get());
	const FTCHARToUTF8 Encoded(*Text);
	if (Encoded.Length() != Bytes.Num() || FMemory::Memcmp(Encoded.Get(), Bytes.GetData(), Bytes.Num()) != 0)
		return Fail(Error, Path + TEXT(": JSON must be valid UTF-8"));
	// UE's reader tolerates a trailing comma in arrays. Shared files use strict
	// JSON on both sides, so reject that extension before the normal token pass.
	bool bQuoted = false, bEscaped = false;
	TCHAR Previous = 0;
	for (const TCHAR Character : Text)
	{
		if (bQuoted)
		{
			if (Character < 0x20) return Fail(Error, Path + TEXT(": unescaped JSON control character"));
			if (bEscaped) bEscaped = false;
			else if (Character == TEXT('\\')) bEscaped = true;
			else if (Character == TEXT('"')) bQuoted = false;
			continue;
		}
		if (Character == TEXT(' ') || Character == TEXT('\t') || Character == TEXT('\r') || Character == TEXT('\n')) continue;
		if ((Character == TEXT(']') || Character == TEXT('}')) && Previous == TEXT(','))
			return Fail(Error, Path + TEXT(": trailing JSON comma"));
		if (Character == TEXT('"')) bQuoted = true;
		Previous = Character;
	}

	// FJsonObject would discard duplicate names. Check decoded identifiers before
	// materializing it, including nested objects and Unicode-escaped names.
	struct FContainer { bool bObject; TSet<FString> Keys; };
	TArray<FContainer> Stack;
	const auto Tokens = TJsonReaderFactory<>::Create(Text);
	EJsonNotation Token;
	while (Tokens->ReadNext(Token))
	{
		if (Token == EJsonNotation::Error) return Fail(Error, Path + TEXT(": ") + Tokens->GetErrorMessage());
		const bool bEnd = Token == EJsonNotation::ObjectEnd || Token == EJsonNotation::ArrayEnd;
		if (!bEnd && !Stack.IsEmpty() && Stack.Last().bObject)
		{
			const FString& Key = Tokens->GetIdentifier();
			if (Stack.Last().Keys.Contains(Key)) return Fail(Error, Path + TEXT(": duplicate JSON key ") + Key);
			Stack.Last().Keys.Add(Key);
		}
		if (Token == EJsonNotation::Number && !FMath::IsFinite(Tokens->GetValueAsNumber()))
			return Fail(Error, Path + TEXT(": non-finite JSON number"));
		if (Token == EJsonNotation::ObjectStart || Token == EJsonNotation::ArrayStart)
		{
			if (Stack.Num() >= 32) return Fail(Error, Path + TEXT(": JSON nesting exceeds 32"));
			Stack.Add({Token == EJsonNotation::ObjectStart, {}});
		}
		else if (bEnd)
		{
			if (Stack.IsEmpty()) return Fail(Error, Path + TEXT(": unbalanced JSON"));
			Stack.Pop(EAllowShrinking::No);
		}
	}
	if (!Tokens->GetErrorMessage().IsEmpty() || !Stack.IsEmpty())
		return Fail(Error, Path + TEXT(": invalid JSON ") + Tokens->GetErrorMessage());
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) || !Out.IsValid())
		return Fail(Error, Path + TEXT(": expected a JSON object"));
	return true;
}

bool Fields(const FObject& Object, std::initializer_list<const TCHAR*> Names, FString& Error,
	std::initializer_list<const TCHAR*> Optional = {})
{
	if (!Object.IsValid()) return Fail(Error, TEXT("expected object"));
	for (const auto& Field : Object->Values)
	{
		bool bKnown = false;
		for (const TCHAR* Name : Names) bKnown |= Field.Key == Name;
		for (const TCHAR* Name : Optional) bKnown |= Field.Key == Name;
		if (!bKnown) return Fail(Error, TEXT("unknown JSON field ") + Field.Key);
	}
	for (const TCHAR* Name : Names)
		if (!Object->HasField(Name)) return Fail(Error, FString(TEXT("missing field ")) + Name);
	return true;
}

bool Text(const FObject& Object, const TCHAR* Key, FString& Out, FString& Error, bool bIdentifier = false)
{
	const auto Value = Object->TryGetField(Key);
	if (!Value.IsValid() || Value->Type != EJson::String || !Value->TryGetString(Out)
		|| Out.IsEmpty() || FTCHARToUTF8(*Out).Length() > 256
		|| Out.Contains(TEXT("\r")) || Out.Contains(TEXT("\n")) || Out.Contains(TEXT("\t")))
		return Fail(Error, FString(Key) + TEXT(": invalid string"));
	for (const TCHAR Character : Out)
		if (Character == 0) return Fail(Error, FString(Key) + TEXT(": embedded NUL"));
	if (bIdentifier)
	{
		if (Out.Len() > 64 || Out[0] < TEXT('a') || Out[0] > TEXT('z'))
			return Fail(Error, FString(Key) + TEXT(": invalid identifier"));
		for (const TCHAR Character : Out)
			if (!((Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_')))
				return Fail(Error, FString(Key) + TEXT(": expected lowercase snake_case identifier"));
	}
	return true;
}

bool SafePartPath(const FString& Path, FString& Error)
{
	if (Path.IsEmpty() || Path.Len() > 256 || !Path.EndsWith(TEXT(".json"), ESearchCase::CaseSensitive))
		return Fail(Error, TEXT("part path must be a relative JSON filename"));
	for (const TCHAR Character : Path)
		if (!((Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'))
			|| Character == TEXT('/') || Character == TEXT('_') || Character == TEXT('-') || Character == TEXT('.')))
			return Fail(Error, TEXT("part path contains unsafe characters"));
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("/"), false);
	for (const FString& Segment : Segments)
		if (Segment.IsEmpty() || Segment == TEXT(".") || Segment == TEXT(".."))
			return Fail(Error, TEXT("part path contains an unsafe segment"));
	return true;
}

bool Number(const FObject& Object, const TCHAR* Key, double Min, double Max, double& Out, FString& Error)
{
	const auto Value = Object->TryGetField(Key);
	if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Out)
		|| !FMath::IsFinite(Out) || Out < Min || Out > Max)
		return Fail(Error, FString(Key) + TEXT(": expected bounded finite number"));
	return true;
}

bool Scalar(const FObject& Object, const TCHAR* Key, double Min, double Max, float& Out, FString& Error)
{
	double Value;
	if (!Number(Object, Key, Min, Max, Value, Error)) return false;
	Out = static_cast<float>(Value);
	return true;
}

bool Vector(const TSharedPtr<FJsonValue>& Value, double Min, double Max, FVector& Out, FString& Error)
{
	const TArray<TSharedPtr<FJsonValue>>* Values;
	if (!Value.IsValid() || Value->Type != EJson::Array || !Value->TryGetArray(Values) || Values->Num() != 3)
		return Fail(Error, TEXT("expected three-number vector"));
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		double Coordinate;
		const auto& Item = (*Values)[Axis];
		if (!Item.IsValid() || Item->Type != EJson::Number || !Item->TryGetNumber(Coordinate)
			|| !FMath::IsFinite(Coordinate) || Coordinate < Min || Coordinate > Max)
			return Fail(Error, TEXT("invalid vector coordinate"));
		Out[Axis] = Coordinate;
	}
	return true;
}

bool Vectors(const FObject& Object, const TCHAR* Key, double Min, double Max, FVector (&Out)[4], FString& Error)
{
	const TArray<TSharedPtr<FJsonValue>>* Values;
	if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 4)
		return Fail(Error, FString(Key) + TEXT(": expected four vectors"));
	for (int32 Index = 0; Index < 4; ++Index)
		if (!Vector((*Values)[Index], Min, Max, Out[Index], Error)) return false;
	return true;
}

struct FPart
{
	FString Path;
	FString Id;
	FString Name;
	FString Kind;
	double MassKg = 0;
	float TireRadiusMeters = 0.0f;
	FString FuelType;
	double IdleRpm = 0;
	double MaxRpm = 0;
	double PeakTorqueNm = 0;
	double MaxInputTorqueNm = 0;
	double FuelCapacityLiters = 0;
	bool bRuntimeFloatCompatible = true;
	FObject Specification;
	TArray<uint8> Bytes;
};

bool RuntimeFloatCompatible(double Value)
{
	const float Converted = static_cast<float>(Value);
	return FMath::IsFinite(Converted) && (Value == 0 || Converted != 0);
}

bool FuelType(const FObject& Object, FString& Out, FString& Error)
{
	if (!Text(Object, TEXT("fuel_type"), Out, Error)) return false;
	return Out == TEXT("gasoline") || Out == TEXT("diesel")
		|| Fail(Error, TEXT("fuel_type must be gasoline or diesel"));
}

bool EnginePart(const FObject& Spec, FPart& Out, FString& Error)
{
	double Value, IdleFuel;
	if (!Fields(Spec, {TEXT("idle_rpm"), TEXT("max_rpm"), TEXT("rotational_inertia_kg_m2"),
		TEXT("response_time_s"), TEXT("idle_fuel_lph"), TEXT("bsfc_g_per_kwh"), TEXT("fuel_type"), TEXT("torque_curve")}, Error)
		|| !Number(Spec, TEXT("idle_rpm"), 100, 10000, Out.IdleRpm, Error)
		|| !Number(Spec, TEXT("max_rpm"), 101, 30000, Out.MaxRpm, Error)
		|| !Number(Spec, TEXT("rotational_inertia_kg_m2"), .0001, 1000, Value, Error)
		|| !Number(Spec, TEXT("response_time_s"), .001, 10, Value, Error)
		|| !Number(Spec, TEXT("idle_fuel_lph"), 0, 100, IdleFuel, Error)
		|| !Number(Spec, TEXT("bsfc_g_per_kwh"), 1, 2000, Value, Error)
		|| !FuelType(Spec, Out.FuelType, Error)) return false;
	if (Out.IdleRpm >= Out.MaxRpm) return Fail(Error, TEXT("idle RPM must be below max RPM"));
	Out.bRuntimeFloatCompatible &= RuntimeFloatCompatible(IdleFuel);
	const TArray<TSharedPtr<FJsonValue>>* Curve;
	if (!Spec->TryGetArrayField(TEXT("torque_curve"), Curve) || Curve->Num() < 2 || Curve->Num() > 128)
		return Fail(Error, TEXT("torque_curve must contain between 2 and 128 points"));
	double FirstRpm = -1, PreviousRpm = -1;
	float PreviousRuntimeRpm = -1;
	for (const auto& Entry : *Curve)
	{
		const FObject* Point;
		double Rpm, Torque;
		if (!Entry.IsValid() || Entry->Type != EJson::Object || !Entry->TryGetObject(Point))
			return Fail(Error, TEXT("torque_curve point must be an object"));
		if (!Fields(*Point, {TEXT("rpm"), TEXT("torque_nm")}, Error)
			|| !Number(*Point, TEXT("rpm"), 0, 30000, Rpm, Error)
			|| !Number(*Point, TEXT("torque_nm"), 0, 100000, Torque, Error)) return false;
		if (Rpm <= PreviousRpm) return Fail(Error, TEXT("torque curve RPM must strictly increase"));
		Out.bRuntimeFloatCompatible &= RuntimeFloatCompatible(Torque) && static_cast<float>(Rpm) > PreviousRuntimeRpm;
		PreviousRuntimeRpm = static_cast<float>(Rpm);
		if (FirstRpm < 0) FirstRpm = Rpm;
		PreviousRpm = Rpm;
		Out.PeakTorqueNm = FMath::Max(Out.PeakTorqueNm, Torque);
	}
	return (FirstRpm == Out.IdleRpm && PreviousRpm == Out.MaxRpm && Out.PeakTorqueNm > 0)
		|| Fail(Error, TEXT("torque curve must span idle_rpm through max_rpm and produce torque"));
}

bool TransmissionPart(const FObject& Spec, FPart& Out, FString& Error)
{
	double Value;
	if (!Fields(Spec, {TEXT("forward_ratios"), TEXT("reverse_ratio"), TEXT("max_input_torque_nm"), TEXT("efficiency")}, Error)
		|| !Number(Spec, TEXT("reverse_ratio"), .01, 100, Value, Error)
		|| !Number(Spec, TEXT("max_input_torque_nm"), .01, 100000, Out.MaxInputTorqueNm, Error)
		|| !Number(Spec, TEXT("efficiency"), .01, 1, Value, Error)) return false;
	const TArray<TSharedPtr<FJsonValue>>* Ratios;
	if (!Spec->TryGetArrayField(TEXT("forward_ratios"), Ratios) || Ratios->IsEmpty() || Ratios->Num() > 20)
		return Fail(Error, TEXT("forward_ratios must contain between 1 and 20 gears"));
	double Previous = 101;
	float PreviousRuntimeRatio = 101;
	for (const auto& Entry : *Ratios)
	{
		double Ratio;
		if (!Entry.IsValid() || Entry->Type != EJson::Number || !Entry->TryGetNumber(Ratio)
			|| !FMath::IsFinite(Ratio) || Ratio < .01 || Ratio > 100 || Ratio >= Previous)
			return Fail(Error, TEXT("forward gear ratios must be bounded positive numbers and strictly decrease"));
		Out.bRuntimeFloatCompatible &= static_cast<float>(Ratio) < PreviousRuntimeRatio;
		PreviousRuntimeRatio = static_cast<float>(Ratio);
		Previous = Ratio;
	}
	return true;
}

bool Part(const FObject& Object, FPart& Out, FString& Error)
{
	double Version, Value;
	FString Interface;
	if (!Fields(Object, {TEXT("schema_version"), TEXT("id"), TEXT("name"), TEXT("kind"),
		TEXT("interface_id"), TEXT("mass"), TEXT("specification")}, Error)
		|| !Number(Object, TEXT("schema_version"), 1, 1, Version, Error)
		|| !Text(Object, TEXT("id"), Out.Id, Error, true)
		|| !Text(Object, TEXT("name"), Out.Name, Error)
		|| !Text(Object, TEXT("kind"), Out.Kind, Error, true)
		|| !Text(Object, TEXT("interface_id"), Interface, Error, true)) return false;
	const FObject* Mass;
	const FObject* Spec;
	FVector Center, Inertia;
	if (!Object->TryGetObjectField(TEXT("mass"), Mass)
		|| !Object->TryGetObjectField(TEXT("specification"), Spec))
		return Fail(Error, TEXT("part mass and specification must be objects"));
	Out.Specification = *Spec;
	if (!Fields(*Mass, {TEXT("mass_kg"), TEXT("center_of_mass_m"), TEXT("inertia_diagonal_kg_m2")}, Error)
		|| !Number(*Mass, TEXT("mass_kg"), .001, 1000000, Out.MassKg, Error)
		|| !Vector((*Mass)->TryGetField(TEXT("center_of_mass_m")), -100, 100, Center, Error)
		|| !Vector((*Mass)->TryGetField(TEXT("inertia_diagonal_kg_m2")), .000001, 1e9, Inertia, Error)) return false;
	for (int32 Axis = 0; Axis < 3; ++Axis)
		if (Inertia[Axis] > Inertia[(Axis + 1) % 3] + Inertia[(Axis + 2) % 3] + 1e-9)
			return Fail(Error, TEXT("part inertia violates rigid-body triangle inequality"));
	if (Out.Kind == TEXT("tire"))
	{
		double Radius, RimDiameter, MinRimWidth, MaxRimWidth;
		if (!Fields(*Spec, {TEXT("radius_m"), TEXT("width_m"), TEXT("rim_diameter_m"), TEXT("min_rim_width_m"),
			TEXT("max_rim_width_m"), TEXT("friction_coefficient"), TEXT("longitudinal_stiffness_n"),
			TEXT("cornering_stiffness_n_rad"), TEXT("rolling_resistance_coefficient"), TEXT("rated_load_n")}, Error)
			|| !Number(*Spec, TEXT("radius_m"), .05, 5, Radius, Error)
			|| !Number(*Spec, TEXT("width_m"), .01, 2, Value, Error)
			|| !Number(*Spec, TEXT("rim_diameter_m"), .05, 3, RimDiameter, Error)
			|| !Number(*Spec, TEXT("min_rim_width_m"), .01, 2, MinRimWidth, Error)
			|| !Number(*Spec, TEXT("max_rim_width_m"), .01, 2, MaxRimWidth, Error)
			|| !Number(*Spec, TEXT("friction_coefficient"), .01, 5, Value, Error)
			|| !Number(*Spec, TEXT("longitudinal_stiffness_n"), 1, 1e8, Value, Error)
			|| !Number(*Spec, TEXT("cornering_stiffness_n_rad"), 1, 1e8, Value, Error)
			|| !Number(*Spec, TEXT("rolling_resistance_coefficient"), 0, 1, Value, Error)
			|| !Number(*Spec, TEXT("rated_load_n"), 1, 1e7, Value, Error)) return false;
		if (Radius * 2 <= RimDiameter || MinRimWidth > MaxRimWidth)
			return Fail(Error, TEXT("invalid tire sidewall or rim width range"));
		Out.TireRadiusMeters = static_cast<float>(Radius);
		return true;
	}
	if (Out.Kind == TEXT("suspension"))
	{
		double Rest, Compression;
		if (!Fields(*Spec, {TEXT("rest_length_m"), TEXT("max_compression_m"), TEXT("max_extension_m"),
			TEXT("spring_rate_n_per_m"), TEXT("damper_rate_n_s_per_m"), TEXT("max_force_n")}, Error)
			|| !Number(*Spec, TEXT("rest_length_m"), .001, 5, Rest, Error)
			|| !Number(*Spec, TEXT("max_compression_m"), 0, 5, Compression, Error)
			|| !Number(*Spec, TEXT("max_extension_m"), 0, 5, Value, Error)
			|| !Number(*Spec, TEXT("spring_rate_n_per_m"), 1, 1e8, Value, Error)
			|| !Number(*Spec, TEXT("damper_rate_n_s_per_m"), 0, 1e7, Value, Error)
			|| !Number(*Spec, TEXT("max_force_n"), 1, 1e8, Value, Error)) return false;
		return Compression < Rest || Fail(Error, TEXT("compression must be shorter than rest length"));
	}
	if (Out.Kind == TEXT("engine")) return EnginePart(*Spec, Out, Error);
	if (Out.Kind == TEXT("transmission")) return TransmissionPart(*Spec, Out, Error);
	if (Out.Kind == TEXT("drivetrain"))
	{
		double FrontTorqueFraction;
		if (!Fields(*Spec, {TEXT("final_drive_ratio"), TEXT("front_torque_fraction"), TEXT("efficiency")}, Error)
			|| !Number(*Spec, TEXT("final_drive_ratio"), .01, 100, Value, Error)
			|| !Number(*Spec, TEXT("front_torque_fraction"), 0, 1, FrontTorqueFraction, Error)
			|| !Number(*Spec, TEXT("efficiency"), .01, 1, Value, Error)) return false;
		Out.bRuntimeFloatCompatible &= RuntimeFloatCompatible(FrontTorqueFraction);
		return true;
	}
	if (Out.Kind == TEXT("fuel_tank"))
	{
		return Fields(*Spec, {TEXT("capacity_l"), TEXT("fuel_density_kg_l"), TEXT("fuel_type")}, Error)
			&& Number(*Spec, TEXT("capacity_l"), .01, 5000, Out.FuelCapacityLiters, Error)
			&& Number(*Spec, TEXT("fuel_density_kg_l"), .1, 2, Value, Error)
			&& FuelType(*Spec, Out.FuelType, Error);
	}
	return Fail(Error, TEXT("unsupported runtime part kind ") + Out.Kind);
}

bool AxleModules(const FObject& Profile, const TMap<FString, FPart>& Parts,
	SimCoreVehicleVisualProfile::FProfile& Out, FString& Error)
{
	if (!Profile->HasField(TEXT("axle_modules"))) return true;
	const FObject* Modules;
	if (!Profile->TryGetObjectField(TEXT("axle_modules"), Modules))
		return Fail(Error, TEXT("axle_modules must be an object"));
	if (!Fields(*Modules, {TEXT("front"), TEXT("rear")}, Error)) return false;
	int32 AxleIndex = 0;
	for (const TCHAR* AxleName : {TEXT("front"), TEXT("rear")})
	{
		const FObject* Axle;
		if (!(*Modules)->TryGetObjectField(AxleName, Axle)) return Fail(Error, TEXT("axle module must be an object"));
		if (!Fields(*Axle, {TEXT("tire"), TEXT("suspension")}, Error)) return false;
		for (const TCHAR* Kind : {TEXT("tire"), TEXT("suspension")})
		{
			if ((*Axle)->TryGetField(Kind)->Type == EJson::Null) continue;
			FString Id;
			if (!Text(*Axle, Kind, Id, Error, true)) return false;
			const FPart* Selected = Parts.Find(Id);
			const bool bTire = FCString::Strcmp(Kind, TEXT("tire")) == 0;
			if (!Selected || Selected->Kind != Kind)
				return Fail(Error, FString(AxleName) + TEXT(".") + Kind + TEXT(": unknown or wrong-kind part ") + Id);
			if (bTire) Out.PlayerAxleTireRadiusMeters[AxleIndex] = Selected->TireRadiusMeters;
		}
		++AxleIndex;
	}
	return true;
}

bool PowertrainModules(const FObject& Profile, const TMap<FString, FPart>& Parts, FString& Error)
{
	const auto Value = Profile->TryGetField(TEXT("powertrain_modules"));
	if (!Value.IsValid() || Value->Type == EJson::Null) return true;
	const FObject* Modules;
	if (Value->Type != EJson::Object || !Value->TryGetObject(Modules))
		return Fail(Error, TEXT("powertrain_modules must be null or an object"));
	if (!Fields(*Modules, {TEXT("engine"), TEXT("transmission"), TEXT("drivetrain"), TEXT("fuel_tank"),
		TEXT("initial_fuel_l"), TEXT("upshift_rpm"), TEXT("downshift_rpm"), TEXT("shift_duration_s")}, Error)) return false;
	const FPart* Selected[4] = {};
	const TCHAR* Kinds[] = {TEXT("engine"), TEXT("transmission"), TEXT("drivetrain"), TEXT("fuel_tank")};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FString Id;
		if (!Text(*Modules, Kinds[Index], Id, Error, true)) return false;
		Selected[Index] = Parts.Find(Id);
		if (!Selected[Index] || Selected[Index]->Kind != Kinds[Index])
			return Fail(Error, FString(Kinds[Index]) + TEXT(": unknown or wrong-kind part ") + Id);
		if (!Selected[Index]->bRuntimeFloatCompatible)
			return Fail(Error, Id + TEXT(": selected part values or ordering cannot be represented at runtime float precision"));
	}
	const auto SelectionNumber = [&Modules, &Error](const TCHAR* Key, double Maximum, double& Out)
	{
		if (!Number(*Modules, Key, 0, Maximum, Out, Error)) return false;
		return RuntimeFloatCompatible(Out)
			? true : Fail(Error, FString(Key) + TEXT(": number outside finite float range"));
	};
	double Fuel, Upshift, Downshift, Duration;
	if (!SelectionNumber(TEXT("initial_fuel_l"), 5000, Fuel)
		|| !SelectionNumber(TEXT("upshift_rpm"), 30000, Upshift)
		|| !SelectionNumber(TEXT("downshift_rpm"), 30000, Downshift)
		|| !SelectionNumber(TEXT("shift_duration_s"), 10, Duration)) return false;
	const FPart& Engine = *Selected[0];
	const FPart& Transmission = *Selected[1];
	const FPart& Tank = *Selected[3];
	if (Engine.FuelType != Tank.FuelType) return Fail(Error, TEXT("engine and fuel tank fuel_type mismatch"));
	if (Engine.PeakTorqueNm > Transmission.MaxInputTorqueNm)
		return Fail(Error, TEXT("engine peak torque exceeds transmission max_input_torque_nm"));
	if (Fuel > Tank.FuelCapacityLiters) return Fail(Error, TEXT("initial_fuel_l exceeds tank capacity"));
	if (!(Engine.IdleRpm < Downshift && Downshift < Upshift && Upshift < Engine.MaxRpm) || Duration <= 0)
		return Fail(Error, TEXT("shift policy requires idle < downshift < upshift < max RPM and positive duration"));
	if (!(static_cast<float>(Engine.IdleRpm) < static_cast<float>(Downshift)
		&& static_cast<float>(Downshift) < static_cast<float>(Upshift)
		&& static_cast<float>(Upshift) < static_cast<float>(Engine.MaxRpm)))
		return Fail(Error, TEXT("shift RPM boundaries collapse at runtime float precision"));
	return true;
}

void DescribeModules(const FObject& Source, const TMap<FString, FPart>& Parts,
	SimCoreVehicleVisualProfile::FProfile& Out)
{
	Out.ModuleDetails.Reset();
	const auto Describe = [&Parts, &Out](const FString& Label, const FObject* Object, const TCHAR* Key)
	{
		FString Id;
		const FPart* Part = Object && (*Object)->TryGetStringField(Key, Id) ? Parts.Find(Id) : nullptr;
		if (!Part) { Out.ModuleDetails.Add(Label + TEXT(": vehicle baseline")); return; }
		FString Detail = Label + TEXT(": ") + Part->Name + FString::Printf(TEXT(" (%.1f kg"), Part->MassKg);
		if (Part->Kind == TEXT("tire")) Detail += FString::Printf(TEXT(", radius %.3f m"), Part->TireRadiusMeters);
		Out.ModuleDetails.Add(Detail + TEXT(")"));
	};
	const FObject* Axles = nullptr;
	Source->TryGetObjectField(TEXT("axle_modules"), Axles);
	for (const TCHAR* Name : {TEXT("front"), TEXT("rear")})
	{
		const FObject* Axle = nullptr;
		if (Axles) (*Axles)->TryGetObjectField(Name, Axle);
		Describe(FString(Name) + TEXT(" tire"), Axle, TEXT("tire"));
		Describe(FString(Name) + TEXT(" suspension"), Axle, TEXT("suspension"));
	}
	const FObject* Powertrain = nullptr;
	if (Source->TryGetObjectField(TEXT("powertrain_modules"), Powertrain))
		for (const TCHAR* Kind : {TEXT("engine"), TEXT("transmission"), TEXT("drivetrain"), TEXT("fuel_tank")})
			Describe(Kind, Powertrain, Kind);
	else Out.ModuleDetails.Add(TEXT("Powertrain: vehicle baseline"));
}

bool Visual(const FObject& Object, SimCoreVehicleVisualProfile::FProfile& Out, FString& Error)
{
	if (!Fields(Object, {TEXT("wheel_origins_cm"), TEXT("wheel_scales"), TEXT("lamp_locations_cm"),
		TEXT("exhaust_location_cm"), TEXT("driver_translation_cm"), TEXT("driver_scale"), TEXT("half_height_m"),
		TEXT("cg_height_m"), TEXT("collision_body_forward_offset_m"), TEXT("collision_ground_clearance_m"),
		TEXT("npc_collision_ground_clearance_m")}, Error)) return false;
	FVector DriverTranslation, DriverScale;
	if (!Vectors(Object, TEXT("wheel_origins_cm"), -5000, 5000, Out.WheelOriginsCm, Error)
		|| !Vectors(Object, TEXT("wheel_scales"), 0.001, 10, Out.WheelScales, Error)
		|| !Vectors(Object, TEXT("lamp_locations_cm"), -5000, 5000, Out.LampLocationsCm, Error)
		|| !Vector(Object->TryGetField(TEXT("exhaust_location_cm")), -5000, 5000, Out.ExhaustLocationCm, Error)
		|| !Vector(Object->TryGetField(TEXT("driver_translation_cm")), -5000, 5000, DriverTranslation, Error)
		|| !Vector(Object->TryGetField(TEXT("driver_scale")), 0.001, 10, DriverScale, Error)
		|| !Scalar(Object, TEXT("half_height_m"), 0.01, 10, Out.HalfHeightMeters, Error)
		|| !Scalar(Object, TEXT("cg_height_m"), 0.01, 10, Out.CgHeightMeters, Error)
		|| !Scalar(Object, TEXT("collision_body_forward_offset_m"), -10, 10, Out.CollisionBodyForwardOffsetMeters, Error)
		|| !Scalar(Object, TEXT("collision_ground_clearance_m"), 0, 5, Out.CollisionGroundClearanceMeters, Error)
		|| !Scalar(Object, TEXT("npc_collision_ground_clearance_m"), 0, 5, Out.NpcCollisionGroundClearanceMeters, Error)) return false;
	Out.DriverTransform = FTransform(FQuat::Identity, DriverTranslation, DriverScale);
	return true;
}

void Feed(uint64& Hash, const uint8* Bytes, int32 Count)
{
	for (int32 Index = 0; Index < Count; ++Index) { Hash ^= Bytes[Index]; Hash *= 1099511628211ULL; }
}

void FeedField(uint64& Hash, const uint8* Bytes, int32 Count)
{
	const FTCHARToUTF8 Prefix(*FString::Printf(TEXT("%d:"), Count));
	Feed(Hash, reinterpret_cast<const uint8*>(Prefix.Get()), Prefix.Length());
	Feed(Hash, Bytes, Count);
}
}

struct SimCoreVehicleVisualProfile::FCatalogSource
{
	TMap<FString, FPart> Parts;
	FObject Profiles[4];
	TMap<FString, FObject> Loadouts;
	TArray<FString> SortedPartIds;
	TArray<FString> SortedLoadoutIds;
};

bool SimCoreVehicleVisualProfile::LoadCatalog(const FString& ManifestPath, FCatalog& OutCatalog, FString& OutError)
{
	OutCatalog = {};
	OutError.Reset();
	FCatalog Candidate;
	const auto SourceDefinitions = MakeShared<FCatalogSource>();
	FObject Manifest;
	TArray<uint8> ManifestBytes;
	const FString FullManifest = FPaths::ConvertRelativePathToFull(ManifestPath);
	if (!ReadDocument(FullManifest, ManifestBytes, Manifest, OutError)) return false;
	double Version;
	if (!Fields(Manifest, {TEXT("schema_version"), TEXT("profiles")}, OutError, {TEXT("parts"), TEXT("loadouts")})
		|| !Number(Manifest, TEXT("schema_version"), 1, 1, Version, OutError)) return false;
	TMap<FString, FPart> Parts;
	TArray<FString> PartIds;
	TSet<FString> SeenPartPaths;
	TSet<FString> SeenFiles;
	FString ManifestKey;
	if (!InsideCatalog(FPaths::GetPath(FullManifest), FullManifest, OutError, &ManifestKey)) return false;
	SeenFiles.Add(ManifestKey);
	int64 TotalBytes = ManifestBytes.Num();
	if (Manifest->HasField(TEXT("parts")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PartPaths;
		if (!Manifest->TryGetArrayField(TEXT("parts"), PartPaths) || PartPaths->Num() > 64)
			return Fail(OutError, TEXT("parts must be an array with at most 64 files"));
		for (const auto& Entry : *PartPaths)
		{
			FPart Loaded;
			if (!Entry.IsValid() || Entry->Type != EJson::String || !Entry->TryGetString(Loaded.Path)
				|| !SafePartPath(Loaded.Path, OutError))
				return Fail(OutError, TEXT("invalid part path: ") + OutError);
			if (SeenPartPaths.Contains(Loaded.Path.ToLower())) return Fail(OutError, TEXT("duplicate part path"));
			SeenPartPaths.Add(Loaded.Path.ToLower());
			const FString Path = FPaths::Combine(FPaths::GetPath(FullManifest), Loaded.Path);
			FString CanonicalKey;
			FObject Document;
			if (!InsideCatalog(FPaths::GetPath(FullManifest), Path, OutError, &CanonicalKey)) return false;
			if (SeenFiles.Contains(CanonicalKey)) return Fail(OutError, TEXT("duplicate catalog file"));
			SeenFiles.Add(CanonicalKey);
			if (!ReadDocument(Path, Loaded.Bytes, Document, OutError)) return false;
			TotalBytes += Loaded.Bytes.Num();
			if (TotalBytes > 16 * 1024 * 1024) return Fail(OutError, TEXT("runtime catalog exceeds 16 MiB"));
			if (!Part(Document, Loaded, OutError))
			{
				OutError = Path + TEXT(": ") + OutError;
				return false;
			}
			if (Parts.Contains(Loaded.Id)) return Fail(OutError, TEXT("duplicate part identifier ") + Loaded.Id);
			for (const TCHAR* VehicleId : VehicleIds)
				if (Loaded.Id == VehicleId) return Fail(OutError, TEXT("part identifier duplicates a vehicle profile"));
			PartIds.Add(Loaded.Id);
			const FString Id = Loaded.Id;
			Parts.Add(Id, MoveTemp(Loaded));
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Paths;
	if (!Manifest->TryGetArrayField(TEXT("profiles"), Paths) || Paths->Num() != 4)
		return Fail(OutError, TEXT("catalog must list exactly four supported profiles"));
	TArray<uint8> ProfileBytes[4];
	TSet<int32> Seen;
	for (const auto& Entry : *Paths)
	{
		FString Relative;
		if (!Entry.IsValid() || Entry->Type != EJson::String || !Entry->TryGetString(Relative))
			return Fail(OutError, TEXT("profile path must be a string"));
		int32 Index = INDEX_NONE;
		for (int32 CandidateIndex = 0; CandidateIndex < 4; ++CandidateIndex)
			if (Relative == ProfilePaths[CandidateIndex]) Index = CandidateIndex;
		if (Index == INDEX_NONE || Seen.Contains(Index)) return Fail(OutError, TEXT("unknown or duplicate profile path"));
		Seen.Add(Index);
		FObject Profile;
		const FString Path = FPaths::Combine(FPaths::GetPath(FullManifest), Relative);
		FString CanonicalKey;
		if (!InsideCatalog(FPaths::GetPath(FullManifest), Path, OutError, &CanonicalKey)) return false;
		if (SeenFiles.Contains(CanonicalKey)) return Fail(OutError, TEXT("duplicate catalog file"));
		SeenFiles.Add(CanonicalKey);
		if (!ReadDocument(Path, ProfileBytes[Index], Profile, OutError)) return false;
		TotalBytes += ProfileBytes[Index].Num();
		if (TotalBytes > 16 * 1024 * 1024) return Fail(OutError, TEXT("runtime catalog exceeds 16 MiB"));
		if (!Fields(Profile, {TEXT("schema_version"), TEXT("id"), TEXT("vehicle_class"), TEXT("player_overrides"),
			TEXT("scale_suspension_by_mass"), TEXT("rest_length_cg_tire_offset_m"), TEXT("npc"), TEXT("visual")}, OutError,
			{TEXT("axle_modules"), TEXT("powertrain_modules")})
			|| !Number(Profile, TEXT("schema_version"), 1, 1, Version, OutError)
			|| !Number(Profile, TEXT("vehicle_class"), Index + 1, Index + 1, Version, OutError)) return false;
		FString Id;
		if (!Profile->TryGetStringField(TEXT("id"), Id) || Id != VehicleIds[Index])
			return Fail(OutError, Path + TEXT(": vehicle ID does not match its frozen class mapping"));
		const auto Overrides = Profile->TryGetField(TEXT("player_overrides"));
		const auto Npc = Profile->TryGetField(TEXT("npc"));
		const auto Scale = Profile->TryGetField(TEXT("scale_suspension_by_mass"));
		const auto RestOffset = Profile->TryGetField(TEXT("rest_length_cg_tire_offset_m"));
		if (!Overrides.IsValid() || Overrides->Type != EJson::Object || !Npc.IsValid() || Npc->Type != EJson::Object
			|| !Scale.IsValid() || Scale->Type != EJson::Boolean || !RestOffset.IsValid()
			|| (RestOffset->Type != EJson::Null && RestOffset->Type != EJson::Number))
			return Fail(OutError, Path + TEXT(": invalid server profile block types"));
		const TSharedPtr<FJsonObject>* VisualObject;
		if (!Profile->TryGetObjectField(TEXT("visual"), VisualObject)
			|| !Visual(*VisualObject, Candidate.Profiles[Index], OutError))
		{
			OutError = Path + TEXT(".visual: ") + OutError;
			return false;
		}
		if (!AxleModules(Profile, Parts, Candidate.Profiles[Index], OutError))
		{
			OutError = Path + TEXT(".axle_modules: ") + OutError;
			return false;
		}
		if (!PowertrainModules(Profile, Parts, OutError))
		{
			OutError = Path + TEXT(".powertrain_modules: ") + OutError;
			return false;
		}
		Candidate.Profiles[Index].VehicleClass = static_cast<SimCoreProtocol::ERuntimeVehicleClass>(Index + 1);
		DescribeModules(Profile, Parts, Candidate.Profiles[Index]);
		Candidate.VehicleIds[Index] = Id;
		SourceDefinitions->Profiles[Index] = Profile;
	}
	struct FLoadoutSource { FString Path; TArray<uint8> Bytes; };
	TArray<FLoadoutSource> LoadoutSources;
	TSet<FString> LoadoutIds;
	if (Manifest->HasField(TEXT("loadouts")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PathsList;
		if (!Manifest->TryGetArrayField(TEXT("loadouts"), PathsList) || PathsList->Num() > 64)
			return Fail(OutError, TEXT("loadouts must contain at most 64 files"));
		for (const auto& Entry : *PathsList)
		{
			FLoadoutSource Source;
			if (!Entry.IsValid() || Entry->Type != EJson::String || !Entry->TryGetString(Source.Path)
				|| !SafePartPath(Source.Path, OutError)) return Fail(OutError, TEXT("invalid loadout path"));
			const FString Path = FPaths::Combine(FPaths::GetPath(FullManifest), Source.Path);
			FString Key;
			if (!InsideCatalog(FPaths::GetPath(FullManifest), Path, OutError, &Key)) return false;
			if (SeenFiles.Contains(Key)) return Fail(OutError, TEXT("duplicate catalog file"));
			SeenFiles.Add(Key);
			FObject Document;
			if (!ReadDocument(Path, Source.Bytes, Document, OutError)) return false;
			TotalBytes += Source.Bytes.Num();
			if (TotalBytes > 16 * 1024 * 1024) return Fail(OutError, TEXT("runtime catalog exceeds 16 MiB"));
			FLoadout Loadout;
			double ClassNumber;
			if (!Fields(Document, {TEXT("schema_version"), TEXT("id"), TEXT("name"), TEXT("vehicle_class"),
				TEXT("axle_modules"), TEXT("powertrain_modules")}, OutError)
				|| !Number(Document, TEXT("schema_version"), 1, 1, Version, OutError)
				|| !Text(Document, TEXT("id"), Loadout.Id, OutError, true)
				|| !Text(Document, TEXT("name"), Loadout.Name, OutError)
				|| !Number(Document, TEXT("vehicle_class"), 1, 4, ClassNumber, OutError)) return false;
			if (ClassNumber != FMath::FloorToDouble(ClassNumber)) return Fail(OutError, TEXT("loadout vehicle_class must be an integer"));
			if (Loadout.Id.StartsWith(TEXT("parts_v1_"), ESearchCase::CaseSensitive))
				return Fail(OutError, TEXT("parts_v1_ is reserved for custom selections"));
			if (LoadoutIds.Contains(Loadout.Id) || Parts.Contains(Loadout.Id)) return Fail(OutError, TEXT("duplicate loadout ID"));
			for (const FString& Id : Candidate.VehicleIds)
				if (Id == Loadout.Id) return Fail(OutError, TEXT("loadout ID duplicates a profile"));
			LoadoutIds.Add(Loadout.Id);
			Loadout.Profile = Candidate.Profiles[static_cast<int32>(ClassNumber) - 1];
			for (auto& Radius : Loadout.Profile.PlayerAxleTireRadiusMeters) Radius.Reset();
			if (!AxleModules(Document, Parts, Loadout.Profile, OutError) || !PowertrainModules(Document, Parts, OutError))
			{
				OutError = Path + TEXT(": ") + OutError;
				return false;
			}
			DescribeModules(Document, Parts, Loadout.Profile);
			SourceDefinitions->Loadouts.Add(Loadout.Id, Document);
			Candidate.Loadouts.Add(MoveTemp(Loadout));
			LoadoutSources.Add(MoveTemp(Source));
		}
	}
	uint64 Hash = 14695981039346656037ULL;
	const ANSICHAR Domain[] = "runtime-vehicle-catalog-v1";
	Feed(Hash, reinterpret_cast<const uint8*>(Domain), UE_ARRAY_COUNT(Domain) - 1);
	FeedField(Hash, ManifestBytes.GetData(), ManifestBytes.Num());
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FTCHARToUTF8 Path(ProfilePaths[Index]);
		FeedField(Hash, reinterpret_cast<const uint8*>(Path.Get()), Path.Length());
		FeedField(Hash, ProfileBytes[Index].GetData(), ProfileBytes[Index].Num());
	}
	PartIds.Sort([&Parts](const FString& A, const FString& B)
	{
		return Parts[A].Path.Compare(Parts[B].Path, ESearchCase::CaseSensitive) < 0;
	});
	for (const FString& Id : PartIds)
	{
		const FPart& Loaded = Parts[Id];
		const FTCHARToUTF8 Path(*Loaded.Path);
		FeedField(Hash, reinterpret_cast<const uint8*>(Path.Get()), Path.Length());
		FeedField(Hash, Loaded.Bytes.GetData(), Loaded.Bytes.Num());
	}
	LoadoutSources.Sort([](const FLoadoutSource& A, const FLoadoutSource& B)
		{ return A.Path.Compare(B.Path, ESearchCase::CaseSensitive) < 0; });
	for (const FLoadoutSource& Source : LoadoutSources)
	{
		const FTCHARToUTF8 Path(*Source.Path);
		FeedField(Hash, reinterpret_cast<const uint8*>(Path.Get()), Path.Length());
		FeedField(Hash, Source.Bytes.GetData(), Source.Bytes.Num());
	}
	Candidate.Checksum = FString::Printf(TEXT("fnv1a64:%016llx"), Hash);
	SourceDefinitions->Parts = MoveTemp(Parts);
	SourceDefinitions->Parts.GetKeys(SourceDefinitions->SortedPartIds);
	SourceDefinitions->Loadouts.GetKeys(SourceDefinitions->SortedLoadoutIds);
	const auto AsciiOrder = [](const FString& A, const FString& B)
		{ return A.Compare(B, ESearchCase::CaseSensitive) < 0; };
	SourceDefinitions->SortedPartIds.Sort(AsciiOrder);
	SourceDefinitions->SortedLoadoutIds.Sort(AsciiOrder);
	Candidate.Source = SourceDefinitions;
	OutCatalog = MoveTemp(Candidate);
	return true;
}

namespace
{
constexpr const TCHAR* CustomPartsPrefix = TEXT("parts_v1_");
constexpr const TCHAR* PartKinds[] = {TEXT("tire"), TEXT("suspension"), TEXT("tire"), TEXT("suspension"),
	TEXT("engine"), TEXT("transmission"), TEXT("drivetrain"), TEXT("fuel_tank")};

bool NamedPartsDraft(const SimCoreVehicleVisualProfile::FCatalog& Catalog,
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass, const FString& Id,
	SimCoreVehicleVisualProfile::FPartsDraft& Out, FString& Error)
{
	if (!Catalog.Source.IsValid() || Catalog.Checksum.IsEmpty()) return Fail(Error, TEXT("Vehicle catalog is unavailable"));
	const FObject* Base = Catalog.Source->Loadouts.Find(Id);
	if (!Base) return Fail(Error, TEXT("Choose a named modular preset before editing its parts"));
	if ((*Base)->GetNumberField(TEXT("vehicle_class")) != static_cast<int32>(VehicleClass))
		return Fail(Error, TEXT("The base preset belongs to another vehicle class"));
	const FObject* Powertrain;
	if (!(*Base)->TryGetObjectField(TEXT("powertrain_modules"), Powertrain))
		return Fail(Error, TEXT("This preset has no complete powertrain policy; choose a modular preset"));
	Out = {};
	Out.BaseLoadoutId = Id;
	const FObject Axles = (*Base)->GetObjectField(TEXT("axle_modules"));
	for (const TCHAR* AxleName : {TEXT("front"), TEXT("rear")})
	{
		const FObject Axle = Axles->GetObjectField(AxleName);
		for (const TCHAR* Kind : {TEXT("tire"), TEXT("suspension")})
		{
			FString PartId;
			Axle->TryGetStringField(Kind, PartId);
			Out.PartIds.Add(MoveTemp(PartId));
		}
	}
	for (int32 Slot = 4; Slot < SimCoreVehicleVisualProfile::PartsSlotCount; ++Slot)
		Out.PartIds.Add((*Powertrain)->GetStringField(PartKinds[Slot]));
	return true;
}

bool ReadHexIndex(const FString& Text, const int32 Offset, int32& Out)
{
	Out = 0;
	for (int32 Digit = 0; Digit < 2; ++Digit)
	{
		const TCHAR C = Text[Offset + Digit];
		const int32 Value = C >= TEXT('0') && C <= TEXT('9') ? C - TEXT('0')
			: C >= TEXT('a') && C <= TEXT('f') ? C - TEXT('a') + 10 : -1;
		if (Value < 0) return false;
		Out = Out * 16 + Value;
	}
	return true;
}

FObject DraftSelection(const SimCoreVehicleVisualProfile::FCatalogSource& Source,
	const SimCoreVehicleVisualProfile::FPartsDraft& Draft)
{
	const auto Result = MakeShared<FJsonObject>();
	const auto Axles = MakeShared<FJsonObject>();
	for (int32 AxleIndex = 0; AxleIndex < 2; ++AxleIndex)
	{
		const auto Axle = MakeShared<FJsonObject>();
		for (int32 Module = 0; Module < 2; ++Module)
		{
			const int32 Slot = AxleIndex * 2 + Module;
			if (Draft.PartIds[Slot].IsEmpty()) Axle->SetField(PartKinds[Slot], MakeShared<FJsonValueNull>());
			else Axle->SetStringField(PartKinds[Slot], Draft.PartIds[Slot]);
		}
		Axles->SetObjectField(AxleIndex == 0 ? TEXT("front") : TEXT("rear"), Axle);
	}
	Result->SetObjectField(TEXT("axle_modules"), Axles);
	const auto Powertrain = MakeShared<FJsonObject>();
	// Only these four IDs change. Fuel and shift policy belong to the named base.
	Powertrain->Values = Source.Loadouts[Draft.BaseLoadoutId]->GetObjectField(TEXT("powertrain_modules"))->Values;
	for (int32 Slot = 4; Slot < SimCoreVehicleVisualProfile::PartsSlotCount; ++Slot)
		Powertrain->SetStringField(PartKinds[Slot], Draft.PartIds[Slot]);
	Result->SetObjectField(TEXT("powertrain_modules"), Powertrain);
	return Result;
}

bool SafeAxleSelection(const SimCoreVehicleVisualProfile::FCatalogSource& Source,
	const int32 ClassIndex, const SimCoreVehicleVisualProfile::FPartsDraft& Draft, FString& Error)
{
	// Preview uses the frozen sedan baseline plus shared class overrides. The
	// server repeats these checks with its actual configured base at reset.
	const FObject Overrides = Source.Profiles[ClassIndex]->GetObjectField(TEXT("player_overrides"));
	const auto Parameter = [&Overrides, &Error](const TCHAR* Key, const float Default, float& Out)
	{
		Out = Default;
		if (!Overrides->HasField(Key)) return true;
		double Value;
		if (!Number(Overrides, Key, 0, 1e9, Value, Error) || !RuntimeFloatCompatible(Value))
			return Fail(Error, FString(Key) + TEXT(": invalid runtime parameter"));
		Out = static_cast<float>(Value);
		return true;
	};
	float Mass, Wheelbase, FrontShare, FrontTrack, RearTrack;
	if (!Parameter(TEXT("mass_kg"), 1500.f, Mass) || !Parameter(TEXT("wheelbase_m"), 2.7f, Wheelbase)
		|| !Parameter(TEXT("front_static_load_fraction"), .55f, FrontShare)
		|| !Parameter(TEXT("front_track_m"), 1.58f, FrontTrack) || !Parameter(TEXT("rear_track_m"), 1.58f, RearTrack)) return false;
	if (Mass <= 0 || Wheelbase <= 0 || FrontTrack <= 0 || RearTrack <= 0 || FrontShare <= 0 || FrontShare >= 1)
		return Fail(Error, TEXT("Invalid shared chassis geometry or static load distribution"));
	bool bSingleTrack = false;
	if (Overrides->HasField(TEXT("single_track")) && !Overrides->TryGetBoolField(TEXT("single_track"), bSingleTrack))
		return Fail(Error, TEXT("single_track must be a boolean"));
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		if (Draft.PartIds[Slot].IsEmpty()) continue;
		const FPart& Selected = Source.Parts[Draft.PartIds[Slot]];
		const FObject Spec = Selected.Specification;
		const bool bFront = Slot < 2;
		const double Share = bFront ? FrontShare : 1.0 - FrontShare;
		const double WheelLoad = Mass * Share * 9.80665 / (bSingleTrack ? 1.0 : 2.0);
		for (const auto& Field : Spec->Values)
		{
			double Value = Field.Value->AsNumber();
			if (bSingleTrack && (Field.Key == TEXT("spring_rate_n_per_m") || Field.Key == TEXT("damper_rate_n_s_per_m")
				|| Field.Key == TEXT("max_force_n") || Field.Key == TEXT("longitudinal_stiffness_n")
				|| Field.Key == TEXT("cornering_stiffness_n_rad"))) Value *= .5;
			if (!RuntimeFloatCompatible(Value)) return Fail(Error, Selected.Id + TEXT(": value is not representable at runtime precision"));
		}
		if (Slot % 2 == 0)
		{
			if (Spec->GetNumberField(TEXT("rated_load_n")) < WheelLoad)
				return Fail(Error, Selected.Id + TEXT(": rated load is below the physical wheel's static load"));
			if (Spec->GetNumberField(TEXT("radius_m")) * 2 >= Wheelbase
				|| Spec->GetNumberField(TEXT("width_m")) >= (bFront ? FrontTrack : RearTrack))
				return Fail(Error, Selected.Id + TEXT(": tire dimensions exceed the supported axle geometry"));
		}
		else if (Spec->GetNumberField(TEXT("max_force_n")) < WheelLoad
			|| Spec->GetNumberField(TEXT("spring_rate_n_per_m")) * Spec->GetNumberField(TEXT("max_compression_m")) < WheelLoad)
			return Fail(Error, Selected.Id + TEXT(": suspension cannot support the physical wheel's static load"));
	}
	return true;
}

FString PartSummary(const FPart& Part)
{
	const FObject Spec = Part.Specification;
	FString Result;
	if (Part.Kind == TEXT("tire"))
		Result = FString::Printf(TEXT("Radius %.3f m | width %.3f m | grip %.2f | rated load %.0f N"),
			Spec->GetNumberField(TEXT("radius_m")), Spec->GetNumberField(TEXT("width_m")),
			Spec->GetNumberField(TEXT("friction_coefficient")), Spec->GetNumberField(TEXT("rated_load_n")));
	else if (Part.Kind == TEXT("suspension"))
		Result = FString::Printf(TEXT("Spring %.0f N/m | damper %.0f N s/m | rest %.3f m | compression %.3f m"),
			Spec->GetNumberField(TEXT("spring_rate_n_per_m")), Spec->GetNumberField(TEXT("damper_rate_n_s_per_m")),
			Spec->GetNumberField(TEXT("rest_length_m")), Spec->GetNumberField(TEXT("max_compression_m")));
	else if (Part.Kind == TEXT("engine"))
		Result = FString::Printf(TEXT("Peak %.0f Nm | %.0f-%.0f RPM | BSFC %.0f g/kWh | %s"),
			Part.PeakTorqueNm, Part.IdleRpm, Part.MaxRpm, Spec->GetNumberField(TEXT("bsfc_g_per_kwh")), *Part.FuelType);
	else if (Part.Kind == TEXT("transmission"))
		Result = FString::Printf(TEXT("%d gears | input limit %.0f Nm | efficiency %.0f%%"),
			Spec->GetArrayField(TEXT("forward_ratios")).Num(), Part.MaxInputTorqueNm, Spec->GetNumberField(TEXT("efficiency")) * 100);
	else if (Part.Kind == TEXT("drivetrain"))
		Result = FString::Printf(TEXT("Final drive %.2f | front torque %.0f%% | efficiency %.0f%%"),
			Spec->GetNumberField(TEXT("final_drive_ratio")), Spec->GetNumberField(TEXT("front_torque_fraction")) * 100,
			Spec->GetNumberField(TEXT("efficiency")) * 100);
	else if (Part.Kind == TEXT("fuel_tank"))
		Result = FString::Printf(TEXT("Capacity %.1f L | density %.3f kg/L | %s"), Part.FuelCapacityLiters,
			Spec->GetNumberField(TEXT("fuel_density_kg_l")), *Part.FuelType);
	return Result + FString::Printf(TEXT(" | mass %.1f kg (metadata only)"), Part.MassKg);
}
}

FString SimCoreVehicleVisualProfile::PartSlotName(const int32 Slot)
{
	const TCHAR* Names[] = {TEXT("Front tire"), TEXT("Front suspension"), TEXT("Rear tire"), TEXT("Rear suspension"),
		TEXT("Engine"), TEXT("Transmission"), TEXT("Drivetrain"), TEXT("Fuel tank")};
	return Slot >= 0 && Slot < PartsSlotCount ? FString(Names[Slot]) : FString();
}

bool SimCoreVehicleVisualProfile::ResolvePartsDraftFromCatalog(const FCatalog& Catalog,
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass, const FPartsDraft& Draft,
	FString& OutId, FProfile& OutProfile, FString& OutError)
{
	OutId.Reset();
	OutProfile = {};
	OutError.Reset();
	FPartsDraft BaseDraft;
	if (!NamedPartsDraft(Catalog, VehicleClass, Draft.BaseLoadoutId, BaseDraft, OutError)) return false;
	if (Draft.PartIds.Num() != PartsSlotCount) return Fail(OutError, TEXT("A selection must contain exactly eight part slots"));
	const FCatalogSource& Source = *Catalog.Source;
	for (int32 Slot = 0; Slot < PartsSlotCount; ++Slot)
	{
		if (Slot < 4 && Draft.PartIds[Slot].IsEmpty()) continue;
		const FPart* Part = Source.Parts.Find(Draft.PartIds[Slot]);
		if (!Part || Part->Kind != PartKinds[Slot])
			return Fail(OutError, PartSlotName(Slot) + TEXT(": select a registered part of the correct kind"));
	}
	const int32 ClassIndex = static_cast<int32>(VehicleClass) - 1;
	if (ClassIndex < 0 || ClassIndex >= 4) return Fail(OutError, TEXT("Unsupported vehicle class"));
	const FObject Selection = DraftSelection(Source, Draft);
	FProfile Candidate = Catalog.Profiles[ClassIndex];
	for (auto& Radius : Candidate.PlayerAxleTireRadiusMeters) Radius.Reset();
	if (!AxleModules(Selection, Source.Parts, Candidate, OutError) || !PowertrainModules(Selection, Source.Parts, OutError)
		|| !SafeAxleSelection(Source, ClassIndex, Draft, OutError)) return false;
	FString Id = Draft.BaseLoadoutId;
	if (Draft.PartIds != BaseDraft.PartIds)
	{
		const int32 BaseIndex = Source.SortedLoadoutIds.IndexOfByKey(Draft.BaseLoadoutId);
		Id = FString::Printf(TEXT("parts_v1_%02x_"), static_cast<uint32>(BaseIndex));
		for (const FString& PartId : Draft.PartIds)
			Id += FString::Printf(TEXT("%02x"), static_cast<uint32>(PartId.IsEmpty() ? 255 : Source.SortedPartIds.IndexOfByKey(PartId)));
	}
	DescribeModules(Selection, Source.Parts, Candidate);
	OutId = MoveTemp(Id);
	OutProfile = MoveTemp(Candidate);
	return true;
}

bool SimCoreVehicleVisualProfile::MakePartsDraftFromCatalog(const FCatalog& Catalog,
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass, const FString& LoadoutId,
	FPartsDraft& OutDraft, FString& OutError)
{
	OutDraft = {};
	OutError.Reset();
	FPartsDraft Candidate;
	if (!LoadoutId.StartsWith(CustomPartsPrefix, ESearchCase::CaseSensitive))
	{
		if (!NamedPartsDraft(Catalog, VehicleClass, LoadoutId, Candidate, OutError)) return false;
		OutDraft = MoveTemp(Candidate);
		return true;
	}
	if (!Catalog.Source.IsValid() || Catalog.Checksum.IsEmpty()) return Fail(OutError, TEXT("Vehicle catalog is unavailable"));
	if (LoadoutId.Len() != 28 || LoadoutId[11] != TEXT('_')) return Fail(OutError, TEXT("Malformed custom selection ID"));
	const FCatalogSource& Source = *Catalog.Source;
	int32 BaseIndex;
	if (!ReadHexIndex(LoadoutId, 9, BaseIndex) || !Source.SortedLoadoutIds.IsValidIndex(BaseIndex))
		return Fail(OutError, TEXT("Custom base index must be in range and lowercase hexadecimal"));
	if (!NamedPartsDraft(Catalog, VehicleClass, Source.SortedLoadoutIds[BaseIndex], Candidate, OutError)) return false;
	for (int32 Slot = 0; Slot < PartsSlotCount; ++Slot)
	{
		int32 PartIndex;
		if (!ReadHexIndex(LoadoutId, 12 + Slot * 2, PartIndex)) return Fail(OutError, TEXT("Custom part index must be lowercase hexadecimal"));
		if (PartIndex == 255 && Slot < 4) Candidate.PartIds[Slot].Reset();
		else if (Source.SortedPartIds.IsValidIndex(PartIndex)) Candidate.PartIds[Slot] = Source.SortedPartIds[PartIndex];
		else return Fail(OutError, TEXT("Custom part index is out of range; only axle parts can use baseline"));
	}
	FString CanonicalId;
	FProfile Preview;
	if (!ResolvePartsDraftFromCatalog(Catalog, VehicleClass, Candidate, CanonicalId, Preview, OutError)) return false;
	if (CanonicalId != LoadoutId) return Fail(OutError, TEXT("Unchanged custom selection must use its named base ID"));
	OutDraft = MoveTemp(Candidate);
	return true;
}

TArray<SimCoreVehicleVisualProfile::FPartChoice> SimCoreVehicleVisualProfile::PartChoicesFromCatalog(
	const FCatalog& Catalog, const FPartsDraft& Draft, const int32 Slot)
{
	TArray<FPartChoice> Result;
	if (!Catalog.Source.IsValid() || Catalog.Checksum.IsEmpty() || Slot < 0 || Slot >= PartsSlotCount
		|| Draft.PartIds.Num() != PartsSlotCount) return Result;
	const FObject* Base = Catalog.Source->Loadouts.Find(Draft.BaseLoadoutId);
	const FObject* Powertrain;
	if (!Base || !(*Base)->TryGetObjectField(TEXT("powertrain_modules"), Powertrain)) return Result;
	if (Slot < 4) Result.Add({FString(), TEXT("Vehicle baseline"), TEXT("Use the class's scalar settings; no module override")});
	for (const FString& Id : Catalog.Source->SortedPartIds)
	{
		const FPart& Part = Catalog.Source->Parts[Id];
		if (Part.Kind == PartKinds[Slot]) Result.Add({Part.Id, Part.Name, PartSummary(Part)});
	}
	return Result;
}
