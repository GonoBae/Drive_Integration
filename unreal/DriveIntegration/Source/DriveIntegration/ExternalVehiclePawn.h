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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle") TObjectPtr<UStaticMeshComponent> VehicleMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SimCore") TObjectPtr<USimCoreClientComponent> SimCoreClient;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore", meta=(ClampMin="1.0")) float PositionInterpolationSpeed = 12.0f;
private:
	void SetThrottle(float Value); void SetBrake(float Value); void SetSteering(float Value);
	void SetHandbrakePressed(); void SetHandbrakeReleased(); void PushControl();
	float ThrottleInput = 0.0f; float BrakeInput = 0.0f; float SteeringInput = 0.0f; bool bHandbrakeInput = false;
};
