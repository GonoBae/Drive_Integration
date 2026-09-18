#include "SimCoreVehicleAudio.h"
#include "SimCoreTurnSignals.h"

namespace
{
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

	float MaximumScrub = 0.0f;
	const float RoadSpeed = FMath::Abs(State.SpeedMps);
	const float CornerSpeedGate = Range01(RoadSpeed, 8.0f, 14.0f);
	for (const SimCoreProtocol::FVehicleState::FWheelState& Wheel : State.Wheels)
	{
		if (!Wheel.bInContact
			|| !FMath::IsFinite(Wheel.SlipAngleRad)
			|| !FMath::IsFinite(Wheel.NormalLoadN) || Wheel.NormalLoadN <= 50.0f
			|| Wheel.WheelIndex >= 4)
		{
			continue;
		}
		const float LoadWeight = FMath::Sqrt(FMath::Clamp(Wheel.NormalLoadN / 3500.0f, 0.0f, 1.0f));
		const float SlipAngle = FMath::Min(FMath::Abs(Wheel.SlipAngleRad), 1.5f);
		const float LateralSlipSpeed = RoadSpeed * FMath::Sin(SlipAngle);
		// This layer is an exceptional cornering skid, not constant road hiss.
		// Require speed, a substantial slip angle AND sideways scrub together.
		// Straight acceleration/wheelspin and ordinary steering do not trigger it.
		const float Scrub = CornerSpeedGate * Range01(SlipAngle, 0.17f, 0.30f)
			* Range01(LateralSlipSpeed, 2.2f, 4.5f) * LoadWeight;
		MaximumScrub = FMath::Max(MaximumScrub, Scrub);
	}

	if (MaximumScrub <= 0.0f)
	{
		return Result;
	}
	Result.TireSlipIntensity = MaximumScrub;
	Result.TireAmplitude = 0.025f * MaximumScrub;
	Result.TireNoiseCutoffHz = 500.0f + 400.0f * MaximumScrub;
	Result.TireSquealAmplitude = 0.22f * MaximumScrub;
	Result.TireSquealFrequencyHz = 650.0f + 350.0f * MaximumScrub + 80.0f * CornerSpeedGate;
	return Result;
}

EIndicatorClick FIndicatorClicks::Update(SimCoreProtocol::ETurnIndicator Direction,
	double TimeSeconds, bool bHazard)
{
	if (bHazard) Direction = SimCoreProtocol::ETurnIndicator::Off;
	const bool bNowSelected = bHazard || Direction == SimCoreProtocol::ETurnIndicator::Left
		|| Direction == SimCoreProtocol::ETurnIndicator::Right;
	if (!FMath::IsFinite(TimeSeconds) || TimeSeconds < 0.0 || !bNowSelected)
	{
		Reset();
		return EIndicatorClick::None;
	}
	const bool bNowLit = USimCoreTurnSignals::IsLit(Direction, true, TimeSeconds, bHazard)
		|| USimCoreTurnSignals::IsLit(Direction, false, TimeSeconds, bHazard);
	const bool bSelectionChanged = bSelected && (Direction != LastDirection || bHazard != bLastHazard);
	LastDirection = Direction;
	bLastHazard = bHazard;
	if (!bSelectionChanged && LastTime >= 0.0 && (TimeSeconds < LastTime || TimeSeconds - LastTime > 1.0))
	{
		// A pause/reset does not represent a physical relay transition.
		bSelected = true; bLit = bNowLit; LastTime = TimeSeconds;
		return EIndicatorClick::None;
	}
	const EIndicatorClick Edge = !bSelected || bSelectionChanged ? (bNowLit ? EIndicatorClick::On : EIndicatorClick::None)
		: bNowLit != bLit ? (bNowLit ? EIndicatorClick::On : EIndicatorClick::Off) : EIndicatorClick::None;
	bSelected = true; bLit = bNowLit; LastTime = TimeSeconds;
	return Edge;
}

void FIndicatorClicks::Reset()
{
	bSelected = false; bLit = false; LastTime = -1.0;
	LastDirection = SimCoreProtocol::ETurnIndicator::Off;
	bLastHazard = false;
}

void FRenderer::SetTargets(const FParameters& Parameters)
{
	Target = Parameters;
	auto Bound = [](float Value, float Maximum)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, Maximum) : 0.0f;
	};
	Target.EngineFrequencyHz = Bound(Target.EngineFrequencyHz, 300.0f);
	Target.EngineAmplitude = Bound(Target.EngineAmplitude, 0.3f);
	Target.TireNoiseCutoffHz = Bound(Target.TireNoiseCutoffHz, 4000.0f);
	Target.TireAmplitude = Bound(Target.TireAmplitude, 0.15f);
	Target.TireSquealFrequencyHz = Bound(Target.TireSquealFrequencyHz, 2500.0f);
	Target.TireSquealAmplitude = Bound(Target.TireSquealAmplitude, 0.3f);
}

void FRenderer::Click(EIndicatorClick Edge)
{
	if (Edge == EIndicatorClick::None) return;
	ClickAge = 0.0f;
	bClickOn = Edge == EIndicatorClick::On;
}

int32 FRenderer::Render(float* OutAudio, int32 NumSamples, int32 SampleRate)
{
	if (!OutAudio || NumSamples <= 0) return 0;
	const float Rate = FMath::Clamp(SampleRate, 8000, 192000);
	const float Fast = 1.0f - FMath::Exp(-1.0f / (0.035f * Rate));
	const float Slow = 1.0f - FMath::Exp(-1.0f / (0.12f * Rate));
	const float Release = 1.0f - FMath::Exp(-1.0f / (0.055f * Rate));
	const float LowAlpha = 1.0f - FMath::Exp(-2.0f * PI * 650.0f / Rate);
	const float HighAlpha = 1.0f - FMath::Exp(-2.0f * PI * FMath::Min(4500.0f, Rate * 0.4f) / Rate);
	const float FlutterAlpha = 1.0f - FMath::Exp(-2.0f * PI * 18.0f / Rate);
	const float TireNoiseGain = FMath::Sqrt(Rate / 48000.0f);
	// Noise-excited rubber resonances, not a free-running whistle. These TPT
	// band-pass filters remain stable across the supported output sample rates.
	auto TireResonance = [Rate](float Noise, float Frequency, float Damping, float* Memory)
	{
		const float G = FMath::Tan(PI * FMath::Clamp(Frequency, 100.0f, Rate * 0.35f) / Rate);
		const float Band = (Memory[0] + G * (Noise - Memory[1])) / (1.0f + G * (G + Damping));
		const float Low = Memory[1] + G * Band;
		Memory[0] = 2.0f * Band - Memory[0];
		Memory[1] = 2.0f * Low - Memory[1];
		return Damping * Band;
	};
	for (int32 Index = 0; Index < NumSamples; Index += 2)
	{
		Current.EngineFrequencyHz += (Target.EngineFrequencyHz - Current.EngineFrequencyHz) * Fast;
		Current.EngineAmplitude += (Target.EngineAmplitude - Current.EngineAmplitude) * Slow;
		Current.TireNoiseCutoffHz += (Target.TireNoiseCutoffHz - Current.TireNoiseCutoffHz) * Fast;
		Current.TireAmplitude += (Target.TireAmplitude - Current.TireAmplitude) * Slow;
		Current.TireSquealFrequencyHz += (Target.TireSquealFrequencyHz - Current.TireSquealFrequencyHz) * Fast;
		Current.TireSquealAmplitude += (Target.TireSquealAmplitude - Current.TireSquealAmplitude)
			* (Target.TireSquealAmplitude < Current.TireSquealAmplitude ? Release : Fast);

		EnginePhase = FMath::Fmod(EnginePhase + 2.0 * PI * Current.EngineFrequencyHz / Rate, 2.0 * PI);
		const float Engine = Current.EngineAmplitude * (0.52f * FMath::Sin(EnginePhase)
			+ 0.28f * FMath::Sin(EnginePhase * 2.0) + 0.12f * FMath::Sin(EnginePhase * 4.0));
		NoiseState ^= NoiseState << 13; NoiseState ^= NoiseState >> 17; NoiseState ^= NoiseState << 5;
		const float Noise = static_cast<float>(NoiseState & 0xffffu) / 32767.5f - 1.0f;
		const float Cutoff = FMath::Clamp(Current.TireNoiseCutoffHz, 100.0f, Rate * 0.4f);
		RollingNoise += (1.0f - FMath::Exp(-2.0f * PI * Cutoff / Rate)) * (Noise - RollingNoise);
		SquealNoiseLow += LowAlpha * (Noise - SquealNoiseLow);
		SquealNoiseHigh += HighAlpha * (Noise - SquealNoiseHigh);
		const float ScrubNoise = SquealNoiseHigh - SquealNoiseLow;
		TireFlutter += FlutterAlpha * (Noise - TireFlutter);
		const float Irregularity = FMath::Clamp(TireFlutter * 8.0f, -1.0f, 1.0f);
		const float ResonanceHz = Current.TireSquealFrequencyHz * (1.0f + 0.045f * Irregularity);
		const float RubberLow = TireResonance(Noise, ResonanceHz, 0.25f, TireResonanceLow);
		const float RubberHigh = TireResonance(Noise, ResonanceHz * 1.63f, 0.45f, TireResonanceHigh);
		// Broad, inharmonic resonance and uneven abrasion avoid the old fixed
		// sine + exact octave's alarm-like tone. The slip envelope still owns
		// onset/release; no periodic tremolo or looping sample is introduced.
		const float Squeal = Current.TireSquealAmplitude * TireNoiseGain * (1.0f + 0.18f * Irregularity)
			* (1.65f * RubberLow + 0.85f * RubberHigh + 0.32f * ScrubNoise);
		float Relay = 0.0f;
		if (ClickAge < 0.025f)
		{
			const float Frequency = bClickOn ? 1750.0f : 1150.0f;
			const float Envelope = FMath::Exp(-ClickAge / 0.0045f) * Range01(ClickAge, 0.0f, 0.0007f);
			Relay = 0.32f * Envelope * (0.65f * FMath::Sin(2.0f * PI * Frequency * ClickAge) + 0.35f * ScrubNoise);
			ClickAge += 1.0f / Rate;
		}
		const float Mixed = FMath::Clamp(Engine + Current.TireAmplitude * RollingNoise + Squeal + Relay, -0.92f, 0.92f);
		OutAudio[Index] = Mixed;
		if (Index + 1 < NumSamples) OutAudio[Index + 1] = Mixed;
	}
	return NumSamples;
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
	const auto Parameters = SimCoreVehicleAudio::BuildParameters(State, StateAgeSeconds, StateStaleTimeoutSeconds);
	bHasFreshState = Parameters.EngineAmplitude > 0.0f;
	if (!bHasFreshState) { Silence(); return; }
	SetTargetParameters(Parameters);
}

void USimCoreVehicleAudioComponent::Silence()
{
	IndicatorClicks.Reset();
	bHasFreshState = false;
	SynthCommand([this]() { Renderer = {}; });
}

void USimCoreVehicleAudioComponent::UpdateIndicator(SimCoreProtocol::ETurnIndicator Direction,
	double TimeSeconds, bool bHazard)
{
	const auto Edge = IndicatorClicks.Update(Direction, TimeSeconds, bHazard);
	// Keep a silent baseline while disconnected. Reconnection must not click
	// halfway through an already lit lamp or replay the missed relay edges.
	if (!bHasFreshState) return;
	if (Edge != SimCoreVehicleAudio::EIndicatorClick::None)
		SynthCommand([this, Edge]() { Renderer.Click(Edge); });
}

void USimCoreVehicleAudioComponent::SetTargetParameters(
	const SimCoreVehicleAudio::FParameters& Parameters)
{
	SynthCommand([this, Parameters]()
	{
		Renderer.SetTargets(Parameters);
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
	return Renderer.Render(OutAudio, NumSamples, RenderSampleRate);
}
