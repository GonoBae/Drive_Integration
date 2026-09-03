#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SimCoreProtocol.h"
#include "SimCoreDriveReplay.generated.h"

class ASimCoreNpcPresentationActor;

namespace SimCoreDriveReplay
{
	inline constexpr int32 MaxFrames = 36000;
	inline constexpr TCHAR FormatName[] = TEXT("simcore-drive-replay-v1");

	struct FFrame
	{
		uint64 SimulationTimeNs = 0;
		uint64 Sequence = 0;
		FVector3d PositionEnu = FVector3d::ZeroVector;
		FVector3d LinearVelocityEnu = FVector3d::ZeroVector;
		float HeadingDegrees = 0.0f;
		float PitchDegrees = 0.0f;
		float RollDegrees = 0.0f;
		float SpeedMps = 0.0f;
		float CollisionHalfLengthMeters = 0.0f;
		float CollisionHalfWidthMeters = 0.0f;
		float CollisionHalfHeightMeters = 0.0f;
	};

	struct FTrack
	{
		FString MapChecksum;
		FString PlaySessionId;
		TArray<FFrame> Frames;

		void Reset();
		bool Capture(const SimCoreProtocol::FVehicleState& State);
		double DurationSeconds() const;
	};

	DRIVEINTEGRATION_API bool Sample(
		const FTrack& Track, double ElapsedSeconds,
		SimCoreProtocol::FVehicleState& OutState);
	DRIVEINTEGRATION_API FString SerializeCsv(const FTrack& Track);
	DRIVEINTEGRATION_API bool ParseCsv(
		const FString& Csv, FTrack& OutTrack, FString& OutError);
}

/** Records accepted authoritative snapshots and replays them as a visual ghost. */
UCLASS(ClassGroup=(SimCore), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreDriveReplayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCoreDriveReplayComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void CaptureAuthoritativeState(const SimCoreProtocol::FVehicleState& State);
	void ToggleRecording();
	void ToggleReplay();
	bool IsRecording() const { return Mode == EMode::Recording; }
	bool IsReplaying() const { return Mode == EMode::Replaying; }
	int32 GetRecordedFrameCount() const { return Track.Frames.Num(); }

	UPROPERTY(EditAnywhere, Category="SimCore|Replay")
	FVector PresentationOffsetCm = FVector::ZeroVector;

private:
	enum class EMode : uint8 { Idle, Recording, Replaying };
	void StopReplay();
	bool SaveLastTrack() const;
	bool LoadLastTrack();
	FString LastTrackPath() const;
	void Notify(const FString& Message, const FColor& Color) const;

	EMode Mode = EMode::Idle;
	SimCoreDriveReplay::FTrack Track;
	double ReplayElapsedSeconds = 0.0;
	UPROPERTY(Transient)
	TObjectPtr<ASimCoreNpcPresentationActor> ReplayGhost;
};
