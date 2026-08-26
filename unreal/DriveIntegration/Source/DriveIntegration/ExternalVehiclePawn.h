#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SimCoreProtocol.h"
#include "ExternalVehiclePawn.generated.h"

class UCameraComponent;
class USceneComponent;
class USimCoreClientComponent;
class USpringArmComponent;
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
	// Unit-scale actor root. Body-only visual scaling must never propagate into
	// wheel centres, wheel dimensions, or the chase camera.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<USceneComponent> PresentationRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UStaticMeshComponent> VehicleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TArray<TObjectPtr<USceneComponent>> WheelPivots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UCameraComponent> Camera;

	// Follows location and yaw but intentionally rejects chassis pitch/roll so
	// suspension attitude remains visible from the chase view.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SimCore")
	TObjectPtr<USimCoreClientComponent> SimCoreClient;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.0", ClampMax="0.1"))
	float MaxExtrapolationSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StateStaleTimeoutSeconds = 0.1f;

	// Common ENU world-origin correction for chassis and contact patches. Keep
	// zero when the MapPackage was baked from this Unreal world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation")
	FVector VisualPositionOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.0", ClampMax="0.5"))
	float VisualWheelStopSpeedMps = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Presentation", meta=(ClampMin="0.1", ClampMax="1.0"))
	float VisualTireRadiusMeters = 0.32f;

	// Opposite pedal brakes first and may select the other direction only near
	// standstill with a fresh authoritative state.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.05", ClampMax="1.0"))
	float DirectionChangeSpeedMps = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.05", ClampMax="1.0"))
	float DirectionStateMaxAgeSeconds = 0.25f;

private:
	void SetThrottle(float Value);
	void SetBrake(float Value);
	void SetSteering(float Value);
	void SetHandbrakePressed();
	void SetHandbrakeReleased();
	void PushControl();

	float ForwardPedalInput = 0.0f;
	float ReversePedalInput = 0.0f;
	float SteeringInput = 0.0f;
	bool bHandbrakeInput = false;
	SimCoreProtocol::EVehicleGear SelectedGear = SimCoreProtocol::EVehicleGear::Drive;
	float FrontAxleSpinDegrees = 0.0f;
	float RearAxleSpinDegrees = 0.0f;
};
