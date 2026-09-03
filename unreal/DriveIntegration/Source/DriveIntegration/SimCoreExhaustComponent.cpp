#include "SimCoreExhaustComponent.h"

#include "Distributions/DistributionFloatParticleParameter.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Materials/MaterialInterface.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Size/ParticleModuleSizeMultiplyLife.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"

namespace
{
	const FName ExhaustSpawnRateParameter(TEXT("SimCoreExhaustSpawnRate"));
}

namespace SimCoreExhaustPresentation
{
	FSample BuildSample(
		float EngineRpm,
		float LongitudinalAccelerationMps2,
		float StateAgeSeconds,
		float StateStaleTimeoutSeconds,
		float IdleSpawnRatePerSecond,
		float MaximumSpawnRatePerSecond)
	{
		FSample Sample;
		if (!FMath::IsFinite(EngineRpm)
			|| !FMath::IsFinite(LongitudinalAccelerationMps2)
			|| !FMath::IsFinite(StateAgeSeconds)
			|| !FMath::IsFinite(StateStaleTimeoutSeconds)
			|| StateStaleTimeoutSeconds < 0.0f
			|| StateAgeSeconds < 0.0f
			|| StateAgeSeconds > StateStaleTimeoutSeconds
			|| EngineRpm < 250.0f)
		{
			return Sample;
		}

		const float SafeIdleRate = FMath::Max(0.0f, IdleSpawnRatePerSecond);
		const float SafeMaximumRate = FMath::Max(SafeIdleRate, MaximumSpawnRatePerSecond);
		const float RpmLoad = FMath::Clamp((EngineRpm - 750.0f) / (6500.0f - 750.0f), 0.0f, 1.0f);
		const float AccelerationLoad = FMath::Clamp(FMath::Abs(LongitudinalAccelerationMps2) / 4.0f, 0.0f, 1.0f);
		// Idle remains faintly visible. RPM gives the sustained flow while a
		// transient acceleration load makes throttle-on puffs denser.
		Sample.Intensity = FMath::Clamp(0.12f + 0.53f * RpmLoad + 0.35f * AccelerationLoad, 0.0f, 1.0f);
		Sample.SpawnRatePerSecond = FMath::Lerp(SafeIdleRate, SafeMaximumRate, Sample.Intensity);
		Sample.bEngineRunning = true;
		return Sample;
	}
}

USimCoreExhaustComponent::USimCoreExhaustComponent()
{
	bAutoActivate = true;
	bAutoDestroy = false;
	bAllowRecycling = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
}

void USimCoreExhaustComponent::OnRegister()
{
	Super::OnRegister();
	EnsureRuntimeTemplate();
}

void USimCoreExhaustComponent::ApplyAuthoritativeState(
	const SimCoreProtocol::FVehicleState& State,
	float StateAgeSeconds,
	float StateStaleTimeoutSeconds,
	float DeltaSeconds)
{
	const SimCoreExhaustPresentation::FSample Sample =
		SimCoreExhaustPresentation::BuildSample(
			State.EngineRpm,
			State.AccelMps2,
			StateAgeSeconds,
			StateStaleTimeoutSeconds,
			IdleSpawnRatePerSecond,
			MaximumSpawnRatePerSecond);
	ApplyTargetRate(Sample.SpawnRatePerSecond, DeltaSeconds);
}

void USimCoreExhaustComponent::ApplyUnavailableState(float DeltaSeconds)
{
	ApplyTargetRate(0.0f, DeltaSeconds);
}

void USimCoreExhaustComponent::ApplyTargetRate(float TargetRatePerSecond, float DeltaSeconds)
{
	const float SafeTarget = FMath::Max(0.0f, TargetRatePerSecond);
	const float SafeDelta = FMath::Max(0.0f, DeltaSeconds);
	CurrentSpawnRatePerSecond = FMath::FInterpTo(
		CurrentSpawnRatePerSecond,
		SafeTarget,
		SafeDelta,
		FMath::Max(0.1f, ResponseSpeed));
	SetFloatParameter(ExhaustSpawnRateParameter, CurrentSpawnRatePerSecond);
}

void USimCoreExhaustComponent::EnsureRuntimeTemplate()
{
	if (Template)
	{
		return;
	}

	UParticleSystem* RuntimeSystem = NewObject<UParticleSystem>(this, NAME_None, RF_Transient);
	UParticleSpriteEmitter* Emitter = NewObject<UParticleSpriteEmitter>(RuntimeSystem, NAME_None, RF_Transient);
	RuntimeSystem->Emitters.Add(Emitter);
	Emitter->SetEmitterName(TEXT("SimCoreExhaust"));

	// Do not use CreateLODLevel/SetToSensibleDefaults here. Those helpers are
	// intended for Cascade authoring and the Editor build calls PostEditChange
	// when launched with -game, where GIsEditor is false. Construct the small
	// transient runtime graph directly so PIE, -game and packaged builds follow
	// the same path.
	UParticleLODLevel* Lod = NewObject<UParticleLODLevel>(Emitter, NAME_None, RF_Transient);
	Lod->Level = 0;
	Lod->bEnabled = true;
	Lod->ConvertedModules = true;
	Lod->PeakActiveParticles = 0;
	Emitter->LODLevels.Add(Lod);

	UParticleModuleRequired* Required =
		NewObject<UParticleModuleRequired>(RuntimeSystem, NAME_None, RF_Transient);
	Required->LODValidity = 1;
	Lod->RequiredModule = Required;
	UParticleModuleSpawn* Spawn =
		NewObject<UParticleModuleSpawn>(RuntimeSystem, NAME_None, RF_Transient);
	Spawn->LODValidity = 1;
	Spawn->BurstList.Empty();
	Lod->SpawnModule = Spawn;

	Required->Material = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/DefaultParticle.DefaultParticle"));
	Required->ScreenAlignment = PSA_FacingCameraPosition;
	Required->bUseLocalSpace = false;
	Required->bKillOnDeactivate = false;
	Required->bKillOnCompleted = false;
	Required->EmitterDuration = 1.0f;
	Required->EmitterLoops = 0;
	Required->bUseMaxDrawCount = true;
	Required->MaxDrawCount = 48;

	UDistributionFloatParticleParameter* SpawnRate =
		NewObject<UDistributionFloatParticleParameter>(Spawn, NAME_None, RF_Transient);
	SpawnRate->ParameterName = ExhaustSpawnRateParameter;
	SpawnRate->MinInput = 0.0f;
	SpawnRate->MaxInput = 60.0f;
	SpawnRate->MinOutput = 0.0f;
	SpawnRate->MaxOutput = 60.0f;
	SpawnRate->ParamMode = DPM_Direct;
	SpawnRate->bIsDirty = true;
	Spawn->Rate.Distribution = SpawnRate;
	Spawn->bApplyGlobalSpawnRateScale = true;

	UParticleModuleLifetime* Lifetime =
		NewObject<UParticleModuleLifetime>(RuntimeSystem, NAME_None, RF_Transient);
	Lifetime->LODValidity = 1;
	UDistributionFloatUniform* LifetimeValue =
		NewObject<UDistributionFloatUniform>(Lifetime, NAME_None, RF_Transient);
	LifetimeValue->Min = 0.75f;
	LifetimeValue->Max = 1.25f;
	LifetimeValue->bIsDirty = true;
	Lifetime->Lifetime.Distribution = LifetimeValue;
	Lod->Modules.Add(Lifetime);

	UParticleModuleSize* Size =
		NewObject<UParticleModuleSize>(RuntimeSystem, NAME_None, RF_Transient);
	Size->LODValidity = 1;
	UDistributionVectorUniform* SizeValue =
		NewObject<UDistributionVectorUniform>(Size, NAME_None, RF_Transient);
	SizeValue->Min = FVector(9.0, 9.0, 9.0);
	SizeValue->Max = FVector(17.0, 17.0, 17.0);
	SizeValue->bIsDirty = true;
	Size->StartSize.Distribution = SizeValue;
	Lod->Modules.Add(Size);

	UParticleModuleVelocity* Velocity =
		NewObject<UParticleModuleVelocity>(RuntimeSystem, NAME_None, RF_Transient);
	Velocity->LODValidity = 1;
	Velocity->bInWorldSpace = false;
	Velocity->bApplyOwnerScale = false;
	UDistributionVectorUniform* VelocityValue =
		NewObject<UDistributionVectorUniform>(Velocity, NAME_None, RF_Transient);
	// Vehicle local +X is forward. The exhaust initially flows out of the
	// tailpipe and rises, then remains in world space behind it.
	VelocityValue->Min = FVector(-42.0, -8.0, 12.0);
	VelocityValue->Max = FVector(-24.0, 8.0, 30.0);
	VelocityValue->bIsDirty = true;
	Velocity->StartVelocity.Distribution = VelocityValue;
	Lod->Modules.Add(Velocity);

	UParticleModuleColorOverLife* Color =
		NewObject<UParticleModuleColorOverLife>(RuntimeSystem, NAME_None, RF_Transient);
	Color->LODValidity = 1;
	UDistributionVectorConstantCurve* ColorCurve =
		NewObject<UDistributionVectorConstantCurve>(Color, NAME_None, RF_Transient);
	Color->ColorOverLife.Distribution = ColorCurve;
	UDistributionFloatConstantCurve* AlphaCurve =
		NewObject<UDistributionFloatConstantCurve>(Color, NAME_None, RF_Transient);
	Color->AlphaOverLife.Distribution = AlphaCurve;
	for (int32 Key = 0; Key < 2; ++Key)
	{
		const float Time = static_cast<float>(Key);
		const int32 ColorKey = ColorCurve->CreateNewKey(Time);
		ColorCurve->SetKeyOut(0, ColorKey, 0.58f);
		ColorCurve->SetKeyOut(1, ColorKey, 0.61f);
		ColorCurve->SetKeyOut(2, ColorKey, 0.65f);
		const int32 AlphaKey = AlphaCurve->CreateNewKey(Time);
		AlphaCurve->SetKeyOut(0, AlphaKey, Key == 0 ? 0.22f : 0.0f);
	}
	ColorCurve->bIsDirty = true;
	AlphaCurve->bIsDirty = true;
	Lod->Modules.Add(Color);

	UParticleModuleSizeMultiplyLife* SizeOverLife =
		NewObject<UParticleModuleSizeMultiplyLife>(RuntimeSystem, NAME_None, RF_Transient);
	SizeOverLife->LODValidity = 1;
	SizeOverLife->MultiplyX = true;
	SizeOverLife->MultiplyY = true;
	SizeOverLife->MultiplyZ = true;
	UDistributionVectorConstantCurve* Curve =
		NewObject<UDistributionVectorConstantCurve>(SizeOverLife, NAME_None, RF_Transient);
	SizeOverLife->LifeMultiplier.Distribution = Curve;
	if (Curve)
	{
		const int32 Start = Curve->CreateNewKey(0.0f);
		const int32 End = Curve->CreateNewKey(1.0f);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Curve->SetKeyOut(Axis, Start, 0.65f);
			Curve->SetKeyOut(Axis, End, 2.4f);
		}
		Curve->bIsDirty = true;
	}
	Lod->Modules.Add(SizeOverLife);

	Emitter->UpdateModuleLists();
	RuntimeSystem->BuildEmitters();
	SetTemplate(RuntimeSystem);
	SetFloatParameter(ExhaustSpawnRateParameter, 0.0f);
}
