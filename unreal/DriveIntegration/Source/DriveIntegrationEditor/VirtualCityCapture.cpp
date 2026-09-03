#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "String/LexFromString.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualCityCapture, Log, All);

namespace
{
/** Editor-module QA helper only; no capture behavior is shipped with the game. */
class FVirtualCityCaptureService
{
public:
	~FVirtualCityCaptureService()
	{
		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		}
	}

	void Start(const TArray<FString>& Args)
	{
		double DelaySeconds = 10.0;
		if (Args.Num() != 1 || !LexTryParseString(DelaySeconds, *Args[0])
			|| !FMath::IsFinite(DelaySeconds) || DelaySeconds < 0.0 || DelaySeconds > 60.0)
		{
			UE_LOG(LogVirtualCityCapture, Error,
				TEXT("Usage: VirtualCity.CaptureAfter <seconds in 0..60>. Uses normal game-thread ticks, not HighResShot render-only warmup."));
			return;
		}
		if (TickerHandle.IsValid())
		{
			UE_LOG(LogVirtualCityCapture, Error, TEXT("A capture is already pending; wait for its saved/failed result."));
			return;
		}
		if (!FApp::CanEverRender() || !GEngine || !GEngine->GameViewport)
		{
			UE_LOG(LogVirtualCityCapture, Error, TEXT("A rendering game viewport is required; NullRHI and a standalone commandlet cannot capture."));
			return;
		}

		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"));
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			UE_LOG(LogVirtualCityCapture, Error, TEXT("Unable to create screenshot directory: %s"), *Directory);
			return;
		}
		OutputPath = Directory / FString::Printf(TEXT("VirtualCity-%s-%s.png"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
		RequestedDelaySeconds = DelaySeconds;
		StartWallTime = FPlatformTime::Seconds();
		NormalTickTime = 0.0;
		NormalTickCount = 0;
		QuietShaderFrames = 0;
		bRequested = false;
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FVirtualCityCaptureService::Tick));
		UE_LOG(LogVirtualCityCapture, Display,
			TEXT("Capture scheduled: delay=%.2fs of normal game-thread ticks, shader-idle gate, timeout=%.2fs, output=%s"),
			DelaySeconds, DelaySeconds + 60.0, *OutputPath);
	}

private:
	bool Finish()
	{
		TickerHandle.Reset();
		return false;
	}

	bool Tick(float DeltaSeconds)
	{
		const double WallElapsed = FPlatformTime::Seconds() - StartWallTime;
		if (bRequested && IFileManager::Get().FileSize(*OutputPath) > 0)
		{
			UE_LOG(LogVirtualCityCapture, Display,
				TEXT("Screenshot saved after %d normal ticks (%.2fs tick time, %.2fs wall time): %s"),
				NormalTickCount, NormalTickTime, WallElapsed, *OutputPath);
			return Finish();
		}
		const int32 RemainingJobs = GShaderCompilingManager
			? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		const bool bShadersBusy = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
		if (WallElapsed >= RequestedDelaySeconds + 60.0)
		{
			UE_LOG(LogVirtualCityCapture, Error,
				TEXT("Capture failed: timed out (requested=%s, normalTicks=%d, normalTickTime=%.2f, shaderJobs=%d, shaderBusy=%s, output=%s)."),
				bRequested ? TEXT("true") : TEXT("false"), NormalTickCount, NormalTickTime,
				RemainingJobs, bShadersBusy ? TEXT("true") : TEXT("false"), *OutputPath);
			return Finish();
		}
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
		{
			UE_LOG(LogVirtualCityCapture, Error, TEXT("Capture failed: the rendering game viewport was closed."));
			return Finish();
		}
		if (bRequested)
		{
			return true; // RequestScreenshot is processed by the next normal viewport draw.
		}

		++NormalTickCount;
		// One cold shader/PSO hitch must not consume the entire requested warmup.
		NormalTickTime += FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, 0.25);
		QuietShaderFrames = bShadersBusy || RemainingJobs != 0 ? 0 : QuietShaderFrames + 1;
		if (NormalTickTime >= RequestedDelaySeconds && NormalTickCount >= 3 && QuietShaderFrames >= 2)
		{
			if (FScreenshotRequest::IsScreenshotRequested())
			{
				return true; // Do not overwrite a screenshot requested by another tool/user.
			}
			FScreenshotRequest::RequestScreenshot(OutputPath, false, false);
			bRequested = true;
			UE_LOG(LogVirtualCityCapture, Display,
				TEXT("Screenshot requested after %d normal ticks; shaderJobs=0 and shader finalization idle: %s"),
				NormalTickCount, *OutputPath);
		}
		return true;
	}

	FTSTicker::FDelegateHandle TickerHandle;
	FString OutputPath;
	double RequestedDelaySeconds = 10.0;
	double StartWallTime = 0.0;
	double NormalTickTime = 0.0;
	int32 NormalTickCount = 0;
	int32 QuietShaderFrames = 0;
	bool bRequested = false;
};

FVirtualCityCaptureService CaptureService;

FAutoConsoleCommand CaptureAfterCommand(
	TEXT("VirtualCity.CaptureAfter"),
	TEXT("Editor-build QA: capture a PNG after <seconds> of normal game-thread ticks and shader completion. In -game, first run 'Module Load DriveIntegrationEditor'."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		CaptureService.Start(Args);
	}));
}
