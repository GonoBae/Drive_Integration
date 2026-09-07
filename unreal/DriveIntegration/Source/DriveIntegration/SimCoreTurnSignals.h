#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCoreProtocol.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreTurnSignals.generated.h"

class UMeshComponent;
class UPointLightComponent;

/** Cosmetic lights only; NPC direction comes from the authoritative route intent. */
UCLASS()
class DRIVEINTEGRATION_API USimCoreTurnSignals : public USceneComponent
{
	GENERATED_BODY()
public:
	USimCoreTurnSignals();
	void BindBodies(UMeshComponent* Source, UMeshComponent* Deformed);
	/**
	 * Repositions the four presentation lamps (front-left, front-right,
	 * rear-left, rear-right) for a different vehicle body. Invalid or partial
	 * profiles are rejected so a model swap cannot leave stale/missing lamps.
	 */
	bool SetLampPositions(TConstArrayView<FVector> PositionsCm);
	void UpdateSignal(SimCoreProtocol::ETurnIndicator Direction, double TimeSeconds, bool bHazard = false);
	void SetBodyDamage(const SimCoreDamagePresentation::FZoneWeights& Weights);
	static bool IsLit(SimCoreProtocol::ETurnIndicator Direction, bool bLeft, double TimeSeconds, bool bHazard);
	TConstArrayView<FVector> GetRestLampPositions() const { return RestLampPositions; }
protected:
	virtual void OnRegister() override;
private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lights;
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> SourceBody;
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> DeformedBody;
	TArray<FVector> RestLampPositions;
};
