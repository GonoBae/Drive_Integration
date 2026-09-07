#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Opt-in, monotonic engine-frame cadence capture; not a GPU timing or input-latency proxy. */
class DRIVEINTEGRATION_API FSimCorePerformanceCapture
{
public:
	~FSimCorePerformanceCapture();
	void StartFromCommandLine(UWorld* World, const FString& MapChecksum);
	void ObserveAcceptedState(double ArrivalSeconds, uint64 Sequence);
	void Stop(const FString& Reason = TEXT("play_ended"));

private:
	struct FSettings
	{
		FIntPoint Resolution = FIntPoint::ZeroValue;
		double MaxFps = -1.0;
		int32 VSync = -1;
		bool bFixedFrameRate = false;
		bool bFixedTimeStep = false;
		bool bSmoothFrameRate = false;
		bool operator==(const FSettings& Other) const;
	};
	struct FSample
	{
		double ElapsedSeconds;
		double ValueMs;
		uint64 Sequence;
		bool bState;
	};
	FSettings ReadSettings() const;
	void OnEndFrame();
	void AddSample(double NowSeconds, double PreviousSeconds, uint64 Sequence, bool bState);
	bool WriteResults(const FString& Reason);

	TWeakObjectPtr<UWorld> CaptureWorld;
	FDelegateHandle EndFrameHandle;
	TArray<FSample> Samples;
	FSettings InitialSettings;
	FSettings LastSettings;
	FString OutputStem;
	FString MapChecksum;
	FString MapName;
	FString Mode;
	double WarmupSeconds = 10.0;
	double RequestedDurationSeconds = 60.0;
	double SampleStartSeconds = 0.0;
	double LastFrameSeconds = 0.0;
	double LastStateSeconds = 0.0;
	double ActualDurationSeconds = 0.0;
	bool bActive = false;
	bool bSettingsCaptured = false;
	bool bSettingsStable = true;
	bool bRenderingEnabled = false;
	static constexpr int32 MaxSamples = 2'000'000;
};
