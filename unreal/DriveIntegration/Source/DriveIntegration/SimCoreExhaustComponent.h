#pragma once

#include "CoreMinimal.h"
#include "Particles/ParticleSystemComponent.h"
#include "SimCoreProtocol.h"
#include "SimCoreExhaustComponent.generated.h"

namespace SimCoreExhaustPresentation
{
	struct FSample
	{
		bool bEngineRunning = false;
		float Intensity = 0.0f;
		float SpawnRatePerSecond = 0.0f;
	};

	/**
	 * Converts authoritative powertrain state into presentation-only exhaust
	 * intensity. This never feeds a value back into SimCore physics.
	 */
	DRIVEINTEGRATION_API FSample BuildSample(
		float EngineRpm,
		float LongitudinalAccelerationMps2,
		float StateAgeSeconds,
		float StateStaleTimeoutSeconds,
		float IdleSpawnRatePerSecond,
		float MaximumSpawnRatePerSecond);
}

/**
 * Asset-free exhaust plume for the player vehicle. The component constructs a
 * small CPU sprite emitter at runtime from Engine content, so a Blueprint or a
 * hand-authored Niagara/Cascade asset is not required for a new project clone.
 */
UCLASS(ClassGroup=(SimCore), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreExhaustComponent : public UParticleSystemComponent
{
	GENERATED_BODY()

public:
	USimCoreExhaustComponent();

	virtual void OnRegister() override;

	void ApplyAuthoritativeState(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float StateStaleTimeoutSeconds,
		float DeltaSeconds);

	void ApplyUnavailableState(float DeltaSeconds);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Exhaust",
		meta=(ClampMin="0.0", ClampMax="20.0"))
	float IdleSpawnRatePerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Exhaust",
		meta=(ClampMin="1.0", ClampMax="60.0"))
	float MaximumSpawnRatePerSecond = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Exhaust",
		meta=(ClampMin="0.1", ClampMax="30.0"))
	float ResponseSpeed = 7.0f;

	UFUNCTION(BlueprintPure, Category="SimCore|Exhaust")
	float GetCurrentSpawnRatePerSecond() const { return CurrentSpawnRatePerSecond; }

private:
	void EnsureRuntimeTemplate();
	void ApplyTargetRate(float TargetRatePerSecond, float DeltaSeconds);

	float CurrentSpawnRatePerSecond = 0.0f;
};
