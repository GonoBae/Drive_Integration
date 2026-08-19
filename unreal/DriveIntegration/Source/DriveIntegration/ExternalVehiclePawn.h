#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ExternalVehiclePawn.generated.h"

class UCameraComponent;
class USimCoreClientComponent;
class UStaticMeshComponent;

UCLASS()
class DRIVEINTEGRATION_API AExternalVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	AExternalVehiclePawn();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UStaticMeshComponent> VehicleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SimCore")
	TObjectPtr<USimCoreClientComponent> SimCoreClient;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.0", ClampMax="0.1"))
	float MaxExtrapolationSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StateStaleTimeoutSeconds = 0.1f;

	// The authoritative PositionEnu Z is 0.55 m with the current default vehicle.
	// A 5 cm presentation offset preserves the prototype's existing 60 cm height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation")
	FVector VisualPositionOffsetCm = FVector(0.0, 0.0, 5.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.0", ClampMax="0.5"))
	float VisualWheelStopSpeedMps = 0.05f;

private:
	void SetThrottle(float Value);
	void SetBrake(float Value);
	void SetSteering(float Value);
	void SetHandbrakePressed();
	void SetHandbrakeReleased();
	void PushControl();

	float ThrottleInput = 0.0f;
	float BrakeInput = 0.0f;
	float SteeringInput = 0.0f;
	bool bHandbrakeInput = false;
	float FrontAxleSpinDegrees = 0.0f;
	float RearAxleSpinDegrees = 0.0f;
};
