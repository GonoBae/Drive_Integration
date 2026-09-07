#include "SimCoreDriveReplay.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SimCoreNpcPresentationActor.h"

USimCoreDriveReplayComponent::USimCoreDriveReplayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USimCoreDriveReplayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopReplay();
	Super::EndPlay(EndPlayReason);
}

void USimCoreDriveReplayComponent::CaptureAuthoritativeState(
	const SimCoreProtocol::FVehicleState& State)
{
	if (Mode == EMode::Recording) Track.Capture(State);
}

FString USimCoreDriveReplayComponent::LastTrackPath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DriveReplays"), TEXT("last_drive.csv"));
}

bool USimCoreDriveReplayComponent::SaveLastTrack() const
{
	const FString Csv = SimCoreDriveReplay::SerializeCsv(Track);
	if (Csv.IsEmpty()) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(LastTrackPath()), true);
	return FFileHelper::SaveStringToFile(Csv, *LastTrackPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool USimCoreDriveReplayComponent::LoadLastTrack()
{
	FString Csv, Error;
	SimCoreDriveReplay::FTrack Loaded;
	if (!FFileHelper::LoadFileToString(Csv, *LastTrackPath())
		|| !SimCoreDriveReplay::ParseCsv(Csv, Loaded, Error)) return false;
	Track = MoveTemp(Loaded);
	return true;
}

void USimCoreDriveReplayComponent::Notify(const FString& Message, const FColor& Color) const
{
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, Color, Message);
}

void USimCoreDriveReplayComponent::ToggleRecording()
{
	if (Mode == EMode::Recording)
	{
		Mode = EMode::Idle;
		const bool bSaved = SaveLastTrack();
		Notify(FString::Printf(TEXT("Drive recording stopped: %d frames%s"),
			Track.Frames.Num(), bSaved ? TEXT(" (saved)") : TEXT("")), FColor::Cyan);
		return;
	}
	StopReplay();
	Track.Reset();
	Mode = EMode::Recording;
	Notify(TEXT("Drive recording started (R to stop)"), FColor::Red);
}

void USimCoreDriveReplayComponent::ToggleReplay()
{
	if (Mode == EMode::Replaying) { StopReplay(); return; }
	if (Mode == EMode::Recording)
	{
		Mode = EMode::Idle;
		SaveLastTrack();
	}
	if (Track.Frames.Num() < 2 && !LoadLastTrack())
	{
		Notify(TEXT("No saved drive replay"), FColor::Yellow);
		return;
	}
	if (!GetWorld()) return;
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	ReplayGhost = GetWorld()->SpawnActor<ASimCoreNpcPresentationActor>(
		ASimCoreNpcPresentationActor::StaticClass(), FTransform::Identity, Spawn);
	if (!ReplayGhost) return;
	Mode = EMode::Replaying;
	ReplayElapsedSeconds = 0.0;
	Notify(TEXT("Drive ghost replay started (F6 to stop)"), FColor::Green);
}

void USimCoreDriveReplayComponent::StopReplay()
{
	if (ReplayGhost) ReplayGhost->Destroy();
	ReplayGhost = nullptr;
	if (Mode == EMode::Replaying) Notify(TEXT("Drive ghost replay stopped"), FColor::Silver);
	Mode = EMode::Idle;
	ReplayElapsedSeconds = 0.0;
}

void USimCoreDriveReplayComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Mode != EMode::Replaying || !ReplayGhost || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f) return;
	SimCoreProtocol::FVehicleState State;
	if (!SimCoreDriveReplay::Sample(Track, ReplayElapsedSeconds, State)) { StopReplay(); return; }
	ReplayGhost->ApplySnapshot(State, 0.0f, DeltaTime, true, 0.0f, PresentationOffsetCm);
	ReplayElapsedSeconds += DeltaTime;
	if (ReplayElapsedSeconds > Track.DurationSeconds()) StopReplay();
}
