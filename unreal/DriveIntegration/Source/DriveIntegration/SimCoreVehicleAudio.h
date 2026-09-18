#pragma once

#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include "SimCoreProtocol.h"
#include "SimCoreVehicleAudio.generated.h"

namespace SimCoreVehicleAudio
{
	/** Audio-render targets derived only from one authoritative host snapshot. */
	struct FParameters
	{
		float EngineFrequencyHz = 0.0f;
		float EngineAmplitude = 0.0f;
		float TireNoiseCutoffHz = 500.0f;
		float TireAmplitude = 0.0f;
		float TireSlipIntensity = 0.0f;
		float TireSquealFrequencyHz = 1000.0f;
		float TireSquealAmplitude = 0.0f;
	};

	DRIVEINTEGRATION_API FParameters BuildParameters(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float StateStaleTimeoutSeconds);

	enum class EIndicatorClick : uint8 { None, On, Off };

	/** Follows the lamp phase; skipped frames never queue a burst of old clicks. */
	struct DRIVEINTEGRATION_API FIndicatorClicks
	{
		EIndicatorClick Update(SimCoreProtocol::ETurnIndicator Direction, double TimeSeconds, bool bHazard);
		void Reset();
	private:
		bool bSelected = false;
		bool bLit = false;
		SimCoreProtocol::ETurnIndicator LastDirection = SimCoreProtocol::ETurnIndicator::Off;
		bool bLastHazard = false;
		double LastTime = -1.0;
	};

	/** Audio-thread state, also renderable offline without an audio device. */
	class DRIVEINTEGRATION_API FRenderer
	{
	public:
		void SetTargets(const FParameters& Parameters);
		void Click(EIndicatorClick Edge);
		int32 Render(float* OutAudio, int32 NumSamples, int32 SampleRate);
	private:
		FParameters Target;
		FParameters Current;
		double EnginePhase = 0.0;
		float RollingNoise = 0.0f;
		float SquealNoiseLow = 0.0f;
		float SquealNoiseHigh = 0.0f;
		float TireFlutter = 0.0f;
		float TireResonanceLow[2] = {};
		float TireResonanceHigh[2] = {};
		float ClickAge = 1.0f;
		bool bClickOn = false;
		uint32 NoiseState = 0x9e3779b9u;
	};
}

/**
 * Asset-free ego-vehicle sound generator. Physics remains server-authoritative;
 * this component only sonifies RPM, road speed, tire contact and published slip.
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreVehicleAudioComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	USimCoreVehicleAudioComponent(const FObjectInitializer& ObjectInitializer);

	void SetAuthoritativeState(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float StateStaleTimeoutSeconds);
	void Silence();
	void UpdateIndicator(SimCoreProtocol::ETurnIndicator Direction, double TimeSeconds, bool bHazard);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	void SetTargetParameters(const SimCoreVehicleAudio::FParameters& Parameters);

	int32 RenderSampleRate = 48000;
	bool bHasFreshState = false;
	SimCoreVehicleAudio::FIndicatorClicks IndicatorClicks;
	SimCoreVehicleAudio::FRenderer Renderer;
};
