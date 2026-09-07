#include "SimCorePerformanceCapture.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProperties.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCorePerformance, Log, All);

FSimCorePerformanceCapture::~FSimCorePerformanceCapture()
{
	Stop();
}

bool FSimCorePerformanceCapture::FSettings::operator==(const FSettings& Other) const
{
	return Resolution == Other.Resolution && MaxFps == Other.MaxFps && VSync == Other.VSync
		&& bFixedFrameRate == Other.bFixedFrameRate && bFixedTimeStep == Other.bFixedTimeStep
		&& bSmoothFrameRate == Other.bSmoothFrameRate;
}

FSimCorePerformanceCapture::FSettings FSimCorePerformanceCapture::ReadSettings() const
{
	FSettings Result;
	Result.bFixedTimeStep = FApp::UseFixedTimeStep();
	if (const UWorld* World = CaptureWorld.Get())
	{
		if (const UGameViewportClient* ViewportClient = World->GetGameViewport())
		{
			if (ViewportClient->Viewport) Result.Resolution = ViewportClient->Viewport->GetSizeXY();
		}
	}
	// These engine CVars live for the process lifetime. Cache lookup only; read
	// their current values every frame so mid-capture changes remain detectable.
	static const IConsoleVariable* MaxFpsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
	static const IConsoleVariable* VSyncCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
	if (MaxFpsCVar) Result.MaxFps = MaxFpsCVar->GetFloat();
	if (VSyncCVar) Result.VSync = VSyncCVar->GetInt();
	if (GEngine)
	{
		Result.bFixedFrameRate = GEngine->bUseFixedFrameRate;
		Result.bSmoothFrameRate = GEngine->bSmoothFrameRate;
	}
	return Result;
}

void FSimCorePerformanceCapture::StartFromCommandLine(UWorld* World, const FString& InMapChecksum)
{
	if (bActive || !World || !FParse::Param(FCommandLine::Get(), TEXT("SimCorePerfCapture"))) return;
	FParse::Value(FCommandLine::Get(), TEXT("SimCorePerfWarmup="), WarmupSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("SimCorePerfDuration="), RequestedDurationSeconds);
	if (!FMath::IsFinite(WarmupSeconds) || WarmupSeconds < 0.0 || WarmupSeconds > 120.0
		|| !FMath::IsFinite(RequestedDurationSeconds) || RequestedDurationSeconds < 0.1
		|| RequestedDurationSeconds > 3600.0)
	{
		UE_LOG(LogSimCorePerformance, Error, TEXT("Capture rejected: warmup must be 0..120s, duration 0.1..3600s."));
		return;
	}
	FString Label = TEXT("drive");
	FParse::Value(FCommandLine::Get(), TEXT("SimCorePerfLabel="), Label);
	Label = FPaths::MakeValidFileName(Label).Left(64);
	OutputStem = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceCaptures"),
		Label + TEXT("_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))
		+ TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	CaptureWorld = World;
	MapChecksum = InMapChecksum;
	MapName = World->GetMapName();
#if WITH_EDITOR
	Mode = World->WorldType == EWorldType::PIE ? TEXT("pie") : TEXT("editor_game");
#else
	Mode = FPlatformProperties::RequiresCookedData() ? TEXT("packaged") : TEXT("uncooked_game");
#endif
	bRenderingEnabled = FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
	Samples.Reset();
	Samples.Reserve(16384);
	LastFrameSeconds = 0.0; // First EndFrame is a baseline, never a partial BeginPlay interval.
	SampleStartSeconds = FPlatformTime::Seconds() + WarmupSeconds;
	LastStateSeconds = 0.0;
	ActualDurationSeconds = 0.0;
	bSettingsCaptured = false;
	bSettingsStable = true;
	bActive = true;
	EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FSimCorePerformanceCapture::OnEndFrame);
	UE_LOG(LogSimCorePerformance, Log,
		TEXT("Capture enabled: mode=%s warmup=%.1fs duration=%.1fs output=%s (engine-frame cadence, not GPU time)"),
		*Mode, WarmupSeconds, RequestedDurationSeconds, *OutputStem);
}

void FSimCorePerformanceCapture::AddSample(
	double NowSeconds, double PreviousSeconds, uint64 Sequence, bool bState)
{
	// Never include an interval crossing the warmup boundary.
	if (PreviousSeconds < SampleStartSeconds || NowSeconds < PreviousSeconds) return;
	if (Samples.Num() >= MaxSamples) { Stop(TEXT("sample_limit")); return; }
	Samples.Add({NowSeconds - SampleStartSeconds, (NowSeconds - PreviousSeconds) * 1000.0, Sequence, bState});
}

void FSimCorePerformanceCapture::OnEndFrame()
{
	if (!bActive) return;
	const double Now = FPlatformTime::Seconds();
	if (Now >= SampleStartSeconds)
	{
		LastSettings = ReadSettings();
		if (!bSettingsCaptured) { InitialSettings = LastSettings; bSettingsCaptured = true; }
		else if (!(InitialSettings == LastSettings)) bSettingsStable = false;
		ActualDurationSeconds = Now - SampleStartSeconds;
		AddSample(Now, LastFrameSeconds, GFrameCounter, false);
		if (bActive && ActualDurationSeconds >= RequestedDurationSeconds) Stop(TEXT("duration_reached"));
	}
	LastFrameSeconds = Now;
}

void FSimCorePerformanceCapture::ObserveAcceptedState(double ArrivalSeconds, uint64 Sequence)
{
	if (!bActive) return;
	AddSample(ArrivalSeconds, LastStateSeconds, Sequence, true);
	LastStateSeconds = ArrivalSeconds;
}

void FSimCorePerformanceCapture::Stop(const FString& Reason)
{
	if (!bActive) return;
	bActive = false;
	FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
	EndFrameHandle.Reset();
	ActualDurationSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - SampleStartSeconds);
	if (!WriteResults(Reason)) UE_LOG(LogSimCorePerformance, Error, TEXT("Capture could not be saved: %s"), *OutputStem);
	Samples.Reset();
}

bool FSimCorePerformanceCapture::WriteResults(const FString& Reason)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputStem), true);
	TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(OutputStem + TEXT(".csv"))));
	if (!Writer) return false;
	auto WriteLine = [&Writer](const FString& Line)
	{
		FTCHARToUTF8 Utf8(*Line);
		Writer->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	};
	WriteLine(TEXT("kind,elapsed_s,value_ms,sequence\n"));
	int32 FrameCount = 0, StateCount = 0;
	for (const FSample& Sample : Samples)
	{
		WriteLine(FString::Printf(TEXT("%s,%.9f,%.9f,%llu\n"), Sample.bState ? TEXT("state_interval") : TEXT("frame"),
			Sample.ElapsedSeconds, Sample.ValueMs, static_cast<unsigned long long>(Sample.Sequence)));
		if (Sample.bState) ++StateCount; else ++FrameCount;
	}
	if (!Writer->Close() || Writer->IsError()) return false;
	Writer.Reset();

	auto SettingsJson = [](const FSettings& Settings)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("width"), Settings.Resolution.X);
		Json->SetNumberField(TEXT("height"), Settings.Resolution.Y);
		Json->SetNumberField(TEXT("max_fps"), Settings.MaxFps);
		Json->SetNumberField(TEXT("vsync"), Settings.VSync);
		Json->SetBoolField(TEXT("fixed_frame_rate"), Settings.bFixedFrameRate);
		Json->SetBoolField(TEXT("fixed_time_step"), Settings.bFixedTimeStep);
		Json->SetBoolField(TEXT("smooth_frame_rate"), Settings.bSmoothFrameRate);
		return Json;
	};
	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	Metadata->SetNumberField(TEXT("schema_version"), 1);
	Metadata->SetStringField(TEXT("frame_metric"), TEXT("monotonic_engine_frame_end_interval_ms"));
	Metadata->SetStringField(TEXT("state_metric"), TEXT("accepted_world_state_game_thread_interval_ms"));
	Metadata->SetStringField(TEXT("input_first_change"), TEXT("not_measured"));
	Metadata->SetStringField(TEXT("manual_driving_acceptance"), TEXT("pending_manual_review"));
	Metadata->SetStringField(TEXT("mode"), Mode);
	Metadata->SetStringField(TEXT("map"), MapName);
	Metadata->SetStringField(TEXT("map_checksum"), MapChecksum);
	Metadata->SetStringField(TEXT("completion_reason"), Reason);
	Metadata->SetBoolField(TEXT("rendering_enabled"), bRenderingEnabled);
	Metadata->SetBoolField(TEXT("settings_captured"), bSettingsCaptured);
	Metadata->SetBoolField(TEXT("settings_stable"), bSettingsStable);
	Metadata->SetObjectField(TEXT("initial_settings"), SettingsJson(InitialSettings));
	Metadata->SetObjectField(TEXT("final_settings"), SettingsJson(LastSettings));
	Metadata->SetNumberField(TEXT("warmup_seconds"), WarmupSeconds);
	Metadata->SetNumberField(TEXT("requested_duration_seconds"), RequestedDurationSeconds);
	Metadata->SetNumberField(TEXT("actual_duration_seconds"), ActualDurationSeconds);
	Metadata->SetNumberField(TEXT("frame_samples"), FrameCount);
	Metadata->SetNumberField(TEXT("state_interval_samples"), StateCount);
	FString JsonText;
	if (!FJsonSerializer::Serialize(Metadata, TJsonWriterFactory<>::Create(&JsonText))) return false;
	if (!FFileHelper::SaveStringToFile(JsonText, *(OutputStem + TEXT(".json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
	UE_LOG(LogSimCorePerformance, Log, TEXT("Capture saved: %s.csv (%d frames, %d state intervals, reason=%s)"),
		*OutputStem, FrameCount, StateCount, *Reason);
	return true;
}
