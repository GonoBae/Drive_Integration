#pragma once

#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include "SimCoreVehicleHorn.generated.h"

namespace SimCoreVehicleHorn
{
	/** Short, recognizable dual-tone road horn. No sound asset is required. */
	inline constexpr float PulseDurationSeconds = 0.38f;
	inline constexpr float AttackSeconds = 0.012f;
	inline constexpr float ReleaseSeconds = 0.065f;
	inline constexpr double MinimumTriggerIntervalSeconds = 0.65;

	/**
	 * Game-thread anti-spam gate for bounded NPC event pulses. Manual key-held
	 * playback is independent; zero means no NPC event occurred in this Play.
	 */
	struct DRIVEINTEGRATION_API FTriggerGate
	{
		bool TryManual(double NowSeconds);
		bool ObserveAuthoritativeEvent(uint32 EventSequence, double NowSeconds);
		void BaselineAuthoritativeEvent(uint32 EventSequence);
		void Reset();

		double LastAcceptedTimeSeconds = -MinimumTriggerIntervalSeconds;
		uint32 LastObservedEventSequence = 0;
	};

	/** Unit envelope used by the audio renderer and deterministic tests. */
	DRIVEINTEGRATION_API float EvaluateEnvelope(float ElapsedSeconds);

	/** Continuous key-held voice, separate from the bounded NPC event envelope. */
	struct DRIVEINTEGRATION_API FHeldEnvelope
	{
		float Advance(float DeltaSeconds, bool bHeld);
		void Reset() { Level = 0.0f; }
	private:
		float Level = 0.0f;
	};
}

/**
 * Asset-free, mono positional car horn. The component renders a dual-tone
 * waveform for either a held player input or a bounded NPC pulse.
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreVehicleHornComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	USimCoreVehicleHornComponent(const FObjectInitializer& ObjectInitializer);

	/** Begin a held H-key voice. Repeated press events do not retrigger it. */
	bool TriggerManualHorn();
	/** Void input delegate kept separate from the testable acceptance result. */
	void HandleHornInput();
	void HandleHornRelease();
	bool IsManualHornHeld() const { return bManualHeld; }
	/** A changed non-zero server-authored horn event sequence for an NPC. */
	bool ObserveAuthoritativeEvent(uint32 EventSequence);
	/** Lifecycle baseline: do not replay an event that predates this actor. */
	void BaselineAuthoritativeEvent(uint32 EventSequence);
	/** Stop current sound without forgetting the accepted event sequence. */
	void SilencePlayback();
	void ResetHornState();

	uint32 GetAcceptedTriggerCount() const { return AcceptedTriggerCount; }
	int32 GetRenderedChannelCount() const { return NumChannels; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	bool TriggerAtTime(double NowSeconds, uint32 EventSequence);
	void QueuePulse();

	SimCoreVehicleHorn::FTriggerGate TriggerGate;
	uint32 AcceptedTriggerCount = 0;
	int32 RenderSampleRate = 48000;
	uint64 PulseFrame = 0;
	double LowTonePhase = 0.0;
	double HighTonePhase = 0.0;
	bool bPulseActive = false;
	bool bManualHeld = false;
	bool bHeldOnAudioThread = false;
	SimCoreVehicleHorn::FHeldEnvelope HeldEnvelope;
};
