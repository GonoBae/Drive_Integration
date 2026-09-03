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
	};

	DRIVEINTEGRATION_API FParameters BuildParameters(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float StateStaleTimeoutSeconds);
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

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	void SetTargetParameters(const SimCoreVehicleAudio::FParameters& Parameters);

	int32 RenderSampleRate = 48000;
	float TargetEngineFrequencyHz = 0.0f;
	float TargetEngineAmplitude = 0.0f;
	float TargetTireNoiseCutoffHz = 500.0f;
	float TargetTireAmplitude = 0.0f;
	float CurrentEngineFrequencyHz = 0.0f;
	float CurrentEngineAmplitude = 0.0f;
	float CurrentTireNoiseCutoffHz = 500.0f;
	float CurrentTireAmplitude = 0.0f;
	double EnginePhase = 0.0;
	float FilteredTireNoise = 0.0f;
	uint32 NoiseState = 0x9e3779b9u;
};
