#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreOrbitCamera.h"
#include "SimCoreProtocol.h"
#include "ExternalVehiclePawn.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class USimCoreClientComponent;
class USimCoreExhaustComponent;
class USimCoreDriveReplayComponent;
class USimCoreDriverPresentation;
class USimCoreSensorRigComponent;
class USimCoreVehicleAudioComponent;
class USimCoreVehicleHornComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UCLASS()
class DRIVEINTEGRATION_API AExternalVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	AExternalVehiclePawn();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	SimCoreProtocol::ETurnIndicator GetManualIndicator() const { return ManualIndicator; }
	bool AreHazardLightsEnabled() const { return bHazardLights; }
	SimCoreProtocol::ERuntimeVehicleClass GetDisplayedVehicleClass() const
	{
		return DisplayedVehicleClass;
	}
	int32 GetVisibleWheelCount() const;
	SimCoreOrbitCamera::EMode GetCameraMode() const { return CameraMode; }
	void SelectPlayerVehicleClass(SimCoreProtocol::ERuntimeVehicleClass VehicleClass);

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

	// Follow mode holds the horizon level. Driver/fixed modes are chassis-relative;
	// V cycles modes and C always restores the rear follow view.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Camera", meta=(ClampMin="0.01", ClampMax="2.0"))
	float CameraMouseDegreesPerInputUnit = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Camera", meta=(ClampMin="1.0", ClampMax="360.0"))
	float CameraGamepadDegreesPerSecond = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Camera", meta=(ClampMin="1.0", ClampMax="300.0"))
	float CameraZoomStepCm = 75.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SimCore")
	TObjectPtr<USimCoreClientComponent> SimCoreClient;

	// Client-side sonification of authoritative RPM, speed, contact and slip.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle|Audio")
	TObjectPtr<USimCoreVehicleAudioComponent> VehicleAudio;

	// H plays one bounded, presentation-only positional horn pulse.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle|Audio")
	TObjectPtr<USimCoreVehicleHornComponent> VehicleHorn;

	// Manny-based seated driver and interior; damage only changes its visible pose.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle|Interior")
	TObjectPtr<USimCoreDriverPresentation> DriverPresentation;

	// Presentation-only plume driven by authoritative engine telemetry.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle|Effects")
	TObjectPtr<USimCoreExhaustComponent> ExhaustEffect;

	// R records authoritative snapshots; F6 replays the last track as a
	// collision-free sedan ghost without taking control from the live vehicle.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle|Replay")
	TObjectPtr<USimCoreDriveReplayComponent> DriveReplay;

	// Metadata-only camera/LiDAR mounts. Capture cadence follows the accepted
	// authoritative simulation clock, never the Unreal render frame rate.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle|Sensors")
	TObjectPtr<USimCoreSensorRigComponent> SensorRig;

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

	// F3 toggles a presentation-only physics overlay. It never participates in
	// collision or sends values back to the authoritative server.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Debug")
	bool bVehicleDebugEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Debug", meta=(ClampMin="0.001", ClampMax="0.1"))
	float DebugForceScaleCmPerNewton = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Debug", meta=(ClampMin="10.0", ClampMax="300.0"))
	float DebugContactNormalLengthCm = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Debug", meta=(ClampMin="25.0", ClampMax="300.0"))
	float DebugSuspensionRayLengthCm = 100.0f;

	// Opposite pedal brakes first and may select the other direction only near
	// standstill with a fresh authoritative state.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.05", ClampMax="1.0"))
	float DirectionChangeSpeedMps = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.05", ClampMax="1.0"))
	float DirectionStateMaxAgeSeconds = 0.25f;

	// Digital A/D steering reaches full command gradually. This is independent
	// of speed; it prevents a short keyboard tap from meaning instant full lock.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.1", ClampMax="5.0"))
	float KeyboardSteeringRiseRatePerSecond = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.1", ClampMax="8.0"))
	float KeyboardSteeringReturnRatePerSecond = 2.0f;

private:
	friend class FSimCorePlayerVehiclePresentationTest;
	friend class FSimCoreCameraModesPawnTest;
	friend class FSimCoreFleetCabinTest;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USimCoreDeformableBody> DeformableBody;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USimCoreTurnSignals> TurnSignals;
	SimCoreProtocol::ETurnIndicator ManualIndicator = SimCoreProtocol::ETurnIndicator::Off;
	bool bHazardLights = false;
	void ToggleLeftIndicator();
	void ToggleRightIndicator();
	void ToggleHazardLights();
	void SelectSedan();
	void SelectCompact();
	void SelectTruck();
	void SelectMotorcycle();
	bool ConfigureVehicleClass(SimCoreProtocol::ERuntimeVehicleClass VehicleClass);
	void OrbitCameraYaw(float Value);
	void OrbitCameraPitch(float Value);
	void SetCameraGamepadYaw(float Value);
	void SetCameraGamepadPitch(float Value);
	void ZoomCamera(float Value);
	void ResetCameraView();
	void CycleCameraView();
	void ToggleVehicleDebug();
	void UpdateOrbitCamera(float DeltaSeconds);
	void UpdateSteeringInput(float DeltaSeconds);
	void InitializeDamagePresentation();
	void ApplyDentMaterialParameters(
		const SimCoreDamagePresentation::FZoneWeights& Weights);
	void ApplyDamagePresentation(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds);
	void DrawVehicleDebug(const SimCoreProtocol::FVehicleState& State) const;
	void SetThrottle(float Value);
	void SetBrake(float Value);
	void SetSteering(float Value);
	void SetSteerLeftPressed();
	void SetSteerLeftReleased();
	void SetSteerRightPressed();
	void SetSteerRightReleased();
	void SetSideBrakePressed();
	void SetSideBrakeReleased();
	void PushControl();

	SimCoreOrbitCamera::FState OrbitCameraState;
	SimCoreOrbitCamera::FDriverLook DriverCameraLook;
	SimCoreOrbitCamera::EMode CameraMode = SimCoreOrbitCamera::EMode::Follow;
	float CameraGamepadYaw = 0.0f;
	float CameraGamepadPitch = 0.0f;

	float ForwardPedalInput = 0.0f;
	float ReversePedalInput = 0.0f;
	float AnalogSteeringInput = 0.0f;
	float KeyboardSteeringInput = 0.0f;
	float SteeringInput = 0.0f;
	bool bSteerLeftPressed = false;
	bool bSteerRightPressed = false;
	bool bSideBrakeInput = false;
	SimCoreProtocol::EVehicleGear SelectedGear = SimCoreProtocol::EVehicleGear::Drive;
	float FrontAxleSpinDegrees = 0.0f;
	float RearAxleSpinDegrees = 0.0f;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DamageMaterialInstances;
	SimCoreDamagePresentation::FAccumulator DamagePresentationAccumulator;
	uint32 LastPresentedCollisionEventSequence = 0;
	TStaticArray<FVector, 4> SuspensionMountLocationsCm;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SedanBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CompactBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> TruckBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> MotorcycleBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SharedWheelMesh;
	SimCoreProtocol::ERuntimeVehicleClass DisplayedVehicleClass =
		SimCoreProtocol::ERuntimeVehicleClass::Unspecified;
};
