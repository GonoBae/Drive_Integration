#include "SimCoreVehicleAudio.h"

namespace
{
constexpr float TireRadiusMeters = 0.32f;

float Range01(float Value, float Start, float End)
{
	return FMath::Clamp((Value - Start) / FMath::Max(End - Start, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
}

}

namespace SimCoreVehicleAudio
{
FParameters BuildParameters(
	const SimCoreProtocol::FVehicleState& State,
	float StateAgeSeconds,
	float StateStaleTimeoutSeconds)
{
	FParameters Result;
	if (!FMath::IsFinite(StateAgeSeconds)
		|| !FMath::IsFinite(StateStaleTimeoutSeconds)
		|| StateAgeSeconds < 0.0f
		|| StateAgeSeconds > FMath::Max(0.0f, StateStaleTimeoutSeconds)
		|| !FMath::IsFinite(State.EngineRpm)
		|| !FMath::IsFinite(State.SpeedMps))
	{
		return Result;
	}

	const float SafeRpm = FMath::Clamp(State.EngineRpm, 650.0f, 7500.0f);
	const float Rpm01 = Range01(SafeRpm, 800.0f, 7000.0f);
	// Four-cylinder firing rate (two combustion events per crank revolution).
	Result.EngineFrequencyHz = SafeRpm / 30.0f;
	Result.EngineAmplitude = 0.075f + 0.13f * FMath::Sqrt(Rpm01);

	float MaximumSlip = 0.0f;
	float MaximumWheelSurfaceSpeed = 0.0f;
	int32 ContactCount = 0;
	for (const SimCoreProtocol::FVehicleState::FWheelState& Wheel : State.Wheels)
	{
		if (!Wheel.bInContact
			|| !FMath::IsFinite(Wheel.LongitudinalSlip)
			|| !FMath::IsFinite(Wheel.SlipAngleRad)
			|| !FMath::IsFinite(Wheel.AngularSpeedRad)
			|| !FMath::IsFinite(Wheel.NormalLoadN))
		{
			continue;
		}
		++ContactCount;
		const float LoadWeight = FMath::Clamp(Wheel.NormalLoadN / 3500.0f, 0.15f, 1.0f);
		const float LongitudinalSlip = Range01(FMath::Abs(Wheel.LongitudinalSlip), 0.05f, 0.35f);
		const float LateralSlip = Range01(FMath::Abs(Wheel.SlipAngleRad), 0.035f, 0.22f);
		MaximumSlip = FMath::Max(MaximumSlip, FMath::Max(LongitudinalSlip, LateralSlip) * LoadWeight);
		MaximumWheelSurfaceSpeed = FMath::Max(
			MaximumWheelSurfaceSpeed,
			FMath::Abs(Wheel.AngularSpeedRad) * TireRadiusMeters);
	}

	if (ContactCount == 0)
	{
		return Result;
	}
	const float ExcitationSpeed = FMath::Max(FMath::Abs(State.SpeedMps), MaximumWheelSurfaceSpeed);
	const float Motion01 = Range01(ExcitationSpeed, 0.35f, 18.0f);
	if (Motion01 <= 0.0f)
	{
		return Result;
	}
	Result.TireSlipIntensity = MaximumSlip;
	Result.TireAmplitude = Motion01 * (0.018f + 0.075f * Motion01 + 0.22f * MaximumSlip);
	Result.TireNoiseCutoffHz = 450.0f + 45.0f * FMath::Min(ExcitationSpeed, 35.0f)
		+ 900.0f * MaximumSlip;
	return Result;
}
}

USimCoreVehicleAudioComponent::USimCoreVehicleAudioComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 2;
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = false;
	bIsUISound = false;
}

void USimCoreVehicleAudioComponent::SetAuthoritativeState(
	const SimCoreProtocol::FVehicleState& State,
	float StateAgeSeconds,
	float StateStaleTimeoutSeconds)
{
	SetTargetParameters(SimCoreVehicleAudio::BuildParameters(
		State, StateAgeSeconds, StateStaleTimeoutSeconds));
}

void USimCoreVehicleAudioComponent::Silence()
{
	SetTargetParameters(SimCoreVehicleAudio::FParameters{});
}

void USimCoreVehicleAudioComponent::SetTargetParameters(
	const SimCoreVehicleAudio::FParameters& Parameters)
{
	SynthCommand([this, Parameters]()
	{
		TargetEngineFrequencyHz = Parameters.EngineFrequencyHz;
		TargetEngineAmplitude = Parameters.EngineAmplitude;
		TargetTireNoiseCutoffHz = Parameters.TireNoiseCutoffHz;
		TargetTireAmplitude = Parameters.TireAmplitude;
	});
}

bool USimCoreVehicleAudioComponent::Init(int32& SampleRate)
{
	RenderSampleRate = FMath::Max(SampleRate, 8000);
	NumChannels = 2;
	return true;
}

int32 USimCoreVehicleAudioComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (!OutAudio || NumSamples <= 0)
	{
		return 0;
	}
	const float SampleRate = static_cast<float>(RenderSampleRate);
	const float FastSmoothing = 1.0f - FMath::Exp(-1.0f / (0.035f * SampleRate));
	const float SlowSmoothing = 1.0f - FMath::Exp(-1.0f / (0.12f * SampleRate));
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex += NumChannels)
	{
		CurrentEngineFrequencyHz += (TargetEngineFrequencyHz - CurrentEngineFrequencyHz) * FastSmoothing;
		CurrentEngineAmplitude += (TargetEngineAmplitude - CurrentEngineAmplitude) * SlowSmoothing;
		CurrentTireNoiseCutoffHz += (TargetTireNoiseCutoffHz - CurrentTireNoiseCutoffHz) * FastSmoothing;
		CurrentTireAmplitude += (TargetTireAmplitude - CurrentTireAmplitude) * SlowSmoothing;

		EnginePhase += 2.0 * PI * static_cast<double>(CurrentEngineFrequencyHz) / SampleRate;
		if (EnginePhase >= 2.0 * PI)
		{
			EnginePhase = FMath::Fmod(EnginePhase, 2.0 * PI);
		}
		const float Engine = CurrentEngineAmplitude * (
			0.52f * FMath::Sin(EnginePhase)
			+ 0.28f * FMath::Sin(EnginePhase * 2.0)
			+ 0.12f * FMath::Sin(EnginePhase * 4.0));

		NoiseState ^= NoiseState << 13;
		NoiseState ^= NoiseState >> 17;
		NoiseState ^= NoiseState << 5;
		const float WhiteNoise = static_cast<float>(NoiseState & 0xffffu) / 32767.5f - 1.0f;
		const float Cutoff = FMath::Clamp(CurrentTireNoiseCutoffHz, 100.0f, SampleRate * 0.45f);
		const float NoiseAlpha = 1.0f - FMath::Exp(-2.0f * PI * Cutoff / SampleRate);
		FilteredTireNoise += NoiseAlpha * (WhiteNoise - FilteredTireNoise);
		const float Tire = CurrentTireAmplitude * FilteredTireNoise;
		const float Mixed = FMath::Clamp(Engine + Tire, -0.92f, 0.92f);
		for (int32 Channel = 0; Channel < NumChannels && SampleIndex + Channel < NumSamples; ++Channel)
		{
			OutAudio[SampleIndex + Channel] = Mixed;
		}
	}
	return NumSamples;
}
