#include "SimCoreVehicleHorn.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Sound/SoundAttenuation.h"

namespace
{
constexpr float LowToneHz = 370.0f;
constexpr float HighToneHz = 466.0f;
constexpr float OutputGain = 0.33f;

bool IsUsableTime(const double NowSeconds)
{
	return FMath::IsFinite(NowSeconds) && NowSeconds >= 0.0;
}
}

namespace SimCoreVehicleHorn
{
bool FTriggerGate::TryManual(const double NowSeconds)
{
	if (!IsUsableTime(NowSeconds)
		|| NowSeconds - LastAcceptedTimeSeconds < MinimumTriggerIntervalSeconds)
	{
		return false;
	}
	LastAcceptedTimeSeconds = NowSeconds;
	return true;
}

bool FTriggerGate::ObserveAuthoritativeEvent(
	const uint32 EventSequence, const double NowSeconds)
{
	if (EventSequence == 0 || EventSequence == LastObservedEventSequence)
	{
		return false;
	}
	// Consume even an over-rate event. Repeated snapshots must not make a
	// rejected server event sound later after the local cooldown expires.
	LastObservedEventSequence = EventSequence;
	return TryManual(NowSeconds);
}

void FTriggerGate::BaselineAuthoritativeEvent(const uint32 EventSequence)
{
	LastObservedEventSequence = EventSequence;
}

void FTriggerGate::Reset()
{
	LastAcceptedTimeSeconds = -MinimumTriggerIntervalSeconds;
	LastObservedEventSequence = 0;
}

float EvaluateEnvelope(const float ElapsedSeconds)
{
	if (!FMath::IsFinite(ElapsedSeconds)
		|| ElapsedSeconds < 0.0f || ElapsedSeconds >= PulseDurationSeconds)
	{
		return 0.0f;
	}
	const float Attack = FMath::Clamp(ElapsedSeconds / AttackSeconds, 0.0f, 1.0f);
	const float Release = FMath::Clamp(
		(PulseDurationSeconds - ElapsedSeconds) / ReleaseSeconds, 0.0f, 1.0f);
	// Smoothstep prevents a click at both pulse edges.
	const float Edge = FMath::Min(Attack, Release);
	return Edge * Edge * (3.0f - 2.0f * Edge);
}

float FHeldEnvelope::Advance(float DeltaSeconds, bool bHeld)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		Reset();
		return 0.0f;
	}
	Level = FMath::Clamp(Level + (bHeld ? DeltaSeconds / AttackSeconds
		: -DeltaSeconds / ReleaseSeconds), 0.0f, 1.0f);
	return Level * Level * (3.0f - 2.0f * Level);
}
}

USimCoreVehicleHornComponent::USimCoreVehicleHornComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = true;
	bIsUISound = false;
	bOverrideAttenuation = true;
	AttenuationOverrides.bAttenuate = true;
	AttenuationOverrides.bSpatialize = true;
	AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	AttenuationOverrides.AttenuationShapeExtents = FVector(200.0f, 0.0f, 0.0f);
	AttenuationOverrides.FalloffDistance = 6000.0f;
	AttenuationOverrides.FalloffMode = ENaturalSoundFalloffMode::Silent;
	AttenuationOverrides.dBAttenuationAtMax = -36.0f;
	AttenuationOverrides.bEnableReverbSend = false;
}

bool USimCoreVehicleHornComponent::TriggerManualHorn()
{
	if (bManualHeld) return false;
	bManualHeld = true;
	++AcceptedTriggerCount;
	SynthCommand([this]() { bHeldOnAudioThread = true; });
	return true;
}

void USimCoreVehicleHornComponent::HandleHornInput()
{
	TriggerManualHorn();
}

void USimCoreVehicleHornComponent::HandleHornRelease()
{
	if (!bManualHeld) return;
	bManualHeld = false;
	SynthCommand([this]() { bHeldOnAudioThread = false; });
}

void USimCoreVehicleHornComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bManualHeld) return;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Player = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	// Key-up can be lost when PIE loses focus or the pawn is unpossessed.
	// Never leave a held audio voice running across pause or input loss.
	if (!Player || !GetWorld() || GetWorld()->IsPaused() || !Player->IsInputKeyDown(EKeys::H))
	{
		HandleHornRelease();
	}
}

bool USimCoreVehicleHornComponent::ObserveAuthoritativeEvent(
	const uint32 EventSequence)
{
	return TriggerAtTime(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0,
		EventSequence);
}

void USimCoreVehicleHornComponent::BaselineAuthoritativeEvent(
	const uint32 EventSequence)
{
	TriggerGate.BaselineAuthoritativeEvent(EventSequence);
}

void USimCoreVehicleHornComponent::ResetHornState()
{
	TriggerGate.Reset();
	SilencePlayback();
}

void USimCoreVehicleHornComponent::SilencePlayback()
{
	bManualHeld = false;
	SynthCommand([this]()
	{
		bPulseActive = false;
		bHeldOnAudioThread = false;
		HeldEnvelope.Reset();
		PulseFrame = 0;
		LowTonePhase = 0.0;
		HighTonePhase = 0.0;
	});
}

bool USimCoreVehicleHornComponent::Init(int32& SampleRate)
{
	RenderSampleRate = FMath::Max(SampleRate, 8000);
	NumChannels = 1;
	return true;
}

int32 USimCoreVehicleHornComponent::OnGenerateAudio(
	float* OutAudio, const int32 NumSamples)
{
	if (!OutAudio || NumSamples <= 0)
	{
		return 0;
	}
	const double SampleRate = static_cast<double>(RenderSampleRate);
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		float Sample = 0.0f;
		float Envelope = HeldEnvelope.Advance(static_cast<float>(1.0 / SampleRate), bHeldOnAudioThread);
		if (bPulseActive)
		{
			const float Elapsed = static_cast<float>(PulseFrame++ / SampleRate);
			Envelope = FMath::Max(Envelope, SimCoreVehicleHorn::EvaluateEnvelope(Elapsed));
			if (Elapsed >= SimCoreVehicleHorn::PulseDurationSeconds)
			{
				bPulseActive = false;
			}
		}
		if (Envelope > 0.0f)
		{
			// A common compact-car interval with mild harmonics reads as a
			// horn while avoiding an abrasive, full-scale square wave.
			const float Tone =
				0.57f * FMath::Sin(LowTonePhase)
				+ 0.38f * FMath::Sin(HighTonePhase)
				+ 0.10f * FMath::Sin(LowTonePhase * 2.0);
			Sample = FMath::Clamp(OutputGain * Envelope * Tone, -0.9f, 0.9f);
			LowTonePhase += 2.0 * PI * LowToneHz / SampleRate;
			HighTonePhase += 2.0 * PI * HighToneHz / SampleRate;
			if (LowTonePhase >= 2.0 * PI) LowTonePhase -= 2.0 * PI;
			if (HighTonePhase >= 2.0 * PI) HighTonePhase -= 2.0 * PI;
		}
		OutAudio[SampleIndex] = Sample;
	}
	return NumSamples;
}

bool USimCoreVehicleHornComponent::TriggerAtTime(
	const double NowSeconds, const uint32 EventSequence)
{
	const bool bAccepted = TriggerGate.ObserveAuthoritativeEvent(EventSequence, NowSeconds);
	if (!bAccepted)
	{
		return false;
	}
	++AcceptedTriggerCount;
	QueuePulse();
	return true;
}

void USimCoreVehicleHornComponent::QueuePulse()
{
	SynthCommand([this]()
	{
		PulseFrame = 0;
		LowTonePhase = 0.0;
		HighTonePhase = 0.0;
		bPulseActive = true;
	});
}
