#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "SimCoreProtocol.h"
#include "SimCoreSensorRig.generated.h"

namespace SimCoreSensorRig
{
	struct FSensorDefinition
	{
		FString SensorId;
		FString Type;
		FString ChildFrame;
		FVector3d PositionFluMeters = FVector3d::ZeroVector;
		// Right-handed base_link FLU roll/pitch/yaw; positive yaw turns left.
		FVector3d RotationFluDegrees = FVector3d::ZeroVector;
		double RateHz = 0.0;
	};

	struct FFrameMetadata
	{
		FString SensorId;
		FString Type;
		FString ParentFrame = TEXT("base_link");
		FString ChildFrame;
		uint64 SourceSequence = 0;
		uint64 SimulationTimeNs = 0;
		FString MapChecksum;
		FString PlaySessionId;
		FTransform MountInUnrealActor;
	};

	DRIVEINTEGRATION_API bool ParseConfigJson(
		const FString& Json, TArray<FSensorDefinition>& OutSensors, FString& OutError);
	DRIVEINTEGRATION_API FTransform BuildMountTransform(
		const FSensorDefinition& Sensor);
}

/** Metadata-only sensor mount/scheduler driven by authoritative simulation time. */
UCLASS(ClassGroup=(SimCore), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreSensorRigComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCoreSensorRigComponent();
	virtual void BeginPlay() override;

	bool LoadConfigFile(const FString& Path, FString& OutError);
	bool InitializeFromJson(const FString& Json, FString& OutError);
	int32 ObserveAuthoritativeState(const SimCoreProtocol::FVehicleState& State);
	const TArray<SimCoreSensorRig::FSensorDefinition>& GetSensors() const { return Sensors; }
	const TArray<SimCoreSensorRig::FFrameMetadata>& GetRecentMetadata() const { return RecentMetadata; }

	UPROPERTY(EditAnywhere, Category="SimCore|Sensors")
	FString ConfigRelativePath = TEXT("sensors.json");

private:
	TArray<SimCoreSensorRig::FSensorDefinition> Sensors;
	TArray<SimCoreSensorRig::FFrameMetadata> RecentMetadata;
	TMap<FString, uint64> LastCaptureTimeBySensor;
	uint64 LastObservedSequence = 0;
};
