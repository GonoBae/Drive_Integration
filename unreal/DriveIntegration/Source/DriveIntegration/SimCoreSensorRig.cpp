#include "SimCoreSensorRig.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SimCoreCoordinateFrames.h"

namespace
{
	bool SafeToken(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 64) return false;
		for (TCHAR Character : Value)
		{
			if (!(FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-')))
				return false;
		}
		return true;
	}

	bool ExactFields(const TSharedPtr<FJsonObject>& Object,
		std::initializer_list<const TCHAR*> Fields)
	{
		if (!Object.IsValid() || Object->Values.Num() != static_cast<int32>(Fields.size())) return false;
		for (const TCHAR* Field : Fields) if (!Object->HasField(Field)) return false;
		return true;
	}

	bool Vector3(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FVector3d& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Name, Values) || !Values || Values->Num() != 3) return false;
		double Components[3]{};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Components[Index])
				|| !FMath::IsFinite(Components[Index])) return false;
		}
		Out = {Components[0], Components[1], Components[2]};
		return true;
	}
}

bool SimCoreSensorRig::ParseConfigJson(
	const FString& Json, TArray<FSensorDefinition>& OutSensors, FString& OutError)
{
	OutSensors.Reset();
	OutError.Reset();
	auto Fail = [&](const TCHAR* Error) { OutSensors.Reset(); OutError = Error; return false; };
	if (Json.IsEmpty() || Json.Len() > 64 * 1024) return Fail(TEXT("Sensor config is empty or oversized"));
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !ExactFields(Root, {TEXT("format_version"), TEXT("sensors")}))
		return Fail(TEXT("Sensor config root is invalid"));
	double Version = 0.0;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Root->TryGetNumberField(TEXT("format_version"), Version) || Version != 1.0
		|| !Root->TryGetArrayField(TEXT("sensors"), Values) || !Values
		|| Values->IsEmpty() || Values->Num() > 16) return Fail(TEXT("Sensor config version/count is invalid"));
	TSet<FString> Ids, Frames;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject>* ObjectPointer = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(ObjectPointer) || !ObjectPointer
			|| !ExactFields(*ObjectPointer, {TEXT("id"), TEXT("type"), TEXT("child_frame"),
				TEXT("position_flu_m"), TEXT("rotation_flu_deg"), TEXT("rate_hz")}))
			return Fail(TEXT("Sensor definition fields are invalid"));
		const auto& Object = *ObjectPointer;
		FSensorDefinition Sensor;
		if (!Object->TryGetStringField(TEXT("id"), Sensor.SensorId)
			|| !Object->TryGetStringField(TEXT("type"), Sensor.Type)
			|| !Object->TryGetStringField(TEXT("child_frame"), Sensor.ChildFrame)
			|| !SafeToken(Sensor.SensorId) || !SafeToken(Sensor.ChildFrame)
			|| (Sensor.Type != TEXT("camera") && Sensor.Type != TEXT("lidar"))
			|| Ids.Contains(Sensor.SensorId) || Frames.Contains(Sensor.ChildFrame)
			|| !Vector3(Object, TEXT("position_flu_m"), Sensor.PositionFluMeters)
			|| !Vector3(Object, TEXT("rotation_flu_deg"), Sensor.RotationFluDegrees)
			|| !Object->TryGetNumberField(TEXT("rate_hz"), Sensor.RateHz)
			|| !FMath::IsFinite(Sensor.RateHz) || Sensor.RateHz < 0.1 || Sensor.RateHz > 120.0
			|| Sensor.PositionFluMeters.GetAbsMax() > 20.0
			|| Sensor.RotationFluDegrees.GetAbsMax() > 360.0)
			return Fail(TEXT("Sensor definition value is invalid"));
		Ids.Add(Sensor.SensorId); Frames.Add(Sensor.ChildFrame);
		OutSensors.Add(MoveTemp(Sensor));
	}
	return true;
}

FTransform SimCoreSensorRig::BuildMountTransform(const FSensorDefinition& Sensor)
{
	const FVector3d UnrealPosition = SimCoreCoordinateFrames::BodyFluPolarVectorToUnrealActor(
		Sensor.PositionFluMeters) * 100.0;
	// Reflection from FLU to Unreal FRU changes the sign of axial X/Z, while
	// pitch about local Y retains its sign.
	const FRotator Rotation(
		Sensor.RotationFluDegrees.Y,
		-Sensor.RotationFluDegrees.Z,
		-Sensor.RotationFluDegrees.X);
	return FTransform(Rotation, FVector(UnrealPosition));
}

USimCoreSensorRigComponent::USimCoreSensorRigComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USimCoreSensorRigComponent::BeginPlay()
{
	Super::BeginPlay();
	FString Error;
	if (!LoadConfigFile(FPaths::Combine(FPaths::ProjectConfigDir(), ConfigRelativePath), Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("SensorRig disabled: %s"), *Error);
	}
}

bool USimCoreSensorRigComponent::LoadConfigFile(const FString& Path, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("Sensor config not found: %s"), *Path);
		return false;
	}
	return InitializeFromJson(Json, OutError);
}

bool USimCoreSensorRigComponent::InitializeFromJson(const FString& Json, FString& OutError)
{
	TArray<SimCoreSensorRig::FSensorDefinition> Parsed;
	if (!SimCoreSensorRig::ParseConfigJson(Json, Parsed, OutError)) return false;
	Sensors = MoveTemp(Parsed);
	RecentMetadata.Reset();
	LastCaptureTimeBySensor.Reset();
	LastObservedSequence = 0;
	return true;
}

int32 USimCoreSensorRigComponent::ObserveAuthoritativeState(
	const SimCoreProtocol::FVehicleState& State)
{
	if (Sensors.IsEmpty() || State.Sequence == 0 || State.Sequence <= LastObservedSequence
		|| State.MapPackageChecksum.IsEmpty() || State.PlaySessionId.IsEmpty()) return 0;
	LastObservedSequence = State.Sequence;
	int32 Emitted = 0;
	for (const auto& Sensor : Sensors)
	{
		const uint64 IntervalNs = static_cast<uint64>(FMath::RoundToDouble(1.e9 / Sensor.RateHz));
		uint64& Last = LastCaptureTimeBySensor.FindOrAdd(Sensor.SensorId);
		if (Last != 0 && (State.SimulationTimeNs < Last
			|| State.SimulationTimeNs - Last < IntervalNs)) continue;
		Last = State.SimulationTimeNs;
		SimCoreSensorRig::FFrameMetadata Metadata;
		Metadata.SensorId = Sensor.SensorId; Metadata.Type = Sensor.Type;
		Metadata.ChildFrame = Sensor.ChildFrame;
		Metadata.SourceSequence = State.Sequence;
		Metadata.SimulationTimeNs = State.SimulationTimeNs;
		Metadata.MapChecksum = State.MapPackageChecksum;
		Metadata.PlaySessionId = State.PlaySessionId;
		Metadata.MountInUnrealActor = SimCoreSensorRig::BuildMountTransform(Sensor);
		RecentMetadata.Add(MoveTemp(Metadata));
		++Emitted;
	}
	if (RecentMetadata.Num() > 256)
		RecentMetadata.RemoveAt(0, RecentMetadata.Num() - 256, EAllowShrinking::No);
	return Emitted;
}
