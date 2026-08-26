#include "ExternalVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "SimCoreClientComponent.h"
#include "SimCoreCoordinateFrames.h"
#include "SimCorePresentation.h"
#include "UObject/ConstructorHelpers.h"

AExternalVehiclePawn::AExternalVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	PresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	SetRootComponent(PresentationRoot);
	VehicleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleMesh"));
	VehicleMesh->SetupAttachment(PresentationRoot);
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		VehicleMesh->SetStaticMesh(Cube.Object);
		// Match the authoritative sedan envelope while leaving the root unscaled.
		VehicleMesh->SetRelativeLocation(FVector(-13.5, 0.0, 0.0));
		VehicleMesh->SetRelativeScale3D(FVector(4.3, 1.8, 0.6));
	}
	const FVector WheelLocations[4] = {
		FVector(121.5, -79.0, -23.0),
		FVector(121.5, 79.0, -23.0),
		FVector(-148.5, -79.0, -23.0),
		FVector(-148.5, 79.0, -23.0),
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		USceneComponent* WheelPivot = CreateDefaultSubobject<USceneComponent>(
			*FString::Printf(TEXT("WheelPivot%d"), Index));
		WheelPivot->SetupAttachment(PresentationRoot);
		WheelPivot->SetRelativeLocation(WheelLocations[Index]);
		WheelPivots.Add(WheelPivot);

		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(WheelPivot);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Cube.Succeeded())
		{
			Wheel->SetStaticMesh(Cube.Object);
		}
		Wheel->SetRelativeScale3D(FVector(0.64, 0.22, 0.64));
		WheelMeshes.Add(Wheel);
	}
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(PresentationRoot);
	CameraBoom->SetRelativeLocation(FVector(0.0, 0.0, 145.0));
	CameraBoom->SetRelativeRotation(FRotator(-12.0, 0.0, 0.0));
	CameraBoom->TargetArmLength = 500.0f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bDoCollisionTest = false;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	SimCoreClient = CreateDefaultSubobject<USimCoreClientComponent>(TEXT("SimCoreClient"));
}

void AExternalVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SimCoreProtocol::FVehicleState State;
	float StateAgeSeconds = 0.0f;
	if (!SimCoreClient->GetLatestState(State, StateAgeSeconds)) return;

	const SimCorePresentation::FVehiclePresentationSample Sample =
		SimCorePresentation::BuildVehicleSample(
			State,
			StateAgeSeconds,
			MaxExtrapolationSeconds,
			StateStaleTimeoutSeconds,
			VisualWheelStopSpeedMps,
			VisualTireRadiusMeters,
			VisualPositionOffsetCm);
	SetActorLocationAndRotation(
		Sample.ActorLocation,
		Sample.ActorRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	FrontAxleSpinDegrees = SimCorePresentation::AdvanceWheelSpinDegrees(
		FrontAxleSpinDegrees,
		Sample.FrontAxleAngularSpeedRadPerSecond,
		DeltaSeconds);
	RearAxleSpinDegrees = SimCorePresentation::AdvanceWheelSpinDegrees(
		RearAxleSpinDegrees,
		Sample.RearAxleAngularSpeedRadPerSecond,
		DeltaSeconds);

	for (const SimCoreProtocol::FVehicleState::FWheelState& WheelState : State.Wheels)
	{
		if (!WheelMeshes.IsValidIndex(WheelState.WheelIndex)
			|| !WheelPivots.IsValidIndex(WheelState.WheelIndex))
		{
			continue;
		}
		const float AxleSpinDegrees = WheelState.WheelIndex < 2
			? FrontAxleSpinDegrees
			: RearAxleSpinDegrees;
		const SimCorePresentation::FWheelGroundPresentationSample& WheelSample =
			Sample.Wheels[WheelState.WheelIndex];
		if (WheelSample.bHasGroundContact)
		{
			WheelPivots[WheelState.WheelIndex]->SetRelativeLocationAndRotation(
				WheelSample.RelativeCenterLocationCm,
				SimCorePresentation::BuildWheelPivotRelativeRotation(
					WheelState,
					WheelSample.RelativeContactNormal));
		}
		WheelMeshes[WheelState.WheelIndex]->SetRelativeRotation(
			SimCorePresentation::BuildWheelSpinRelativeRotation(AxleSpinDegrees));
	}
}

void AExternalVehiclePawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	Input->BindAxis(TEXT("Throttle"), this, &AExternalVehiclePawn::SetThrottle);
	Input->BindAxis(TEXT("Brake"), this, &AExternalVehiclePawn::SetBrake);
	Input->BindAxis(TEXT("Steering"), this, &AExternalVehiclePawn::SetSteering);
	Input->BindAction(TEXT("Handbrake"), IE_Pressed, this, &AExternalVehiclePawn::SetHandbrakePressed);
	Input->BindAction(TEXT("Handbrake"), IE_Released, this, &AExternalVehiclePawn::SetHandbrakeReleased);
}

void AExternalVehiclePawn::SetThrottle(float Value)
{
	ForwardPedalInput = FMath::Clamp(Value, 0.0f, 1.0f);
	PushControl();
}

void AExternalVehiclePawn::SetBrake(float Value)
{
	ReversePedalInput = FMath::Clamp(Value, 0.0f, 1.0f);
	PushControl();
}

void AExternalVehiclePawn::SetSteering(float Value)
{
	// Unreal input axes and the device X axis are right-positive. Convert once
	// at the public protocol boundary; ControlCommand schema v2 is left-positive.
	SteeringInput = SimCoreCoordinateFrames::InputAxisToCanonicalSteering(Value);
	PushControl();
}

void AExternalVehiclePawn::SetHandbrakePressed()
{
	bHandbrakeInput = true;
	PushControl();
}

void AExternalVehiclePawn::SetHandbrakeReleased()
{
	bHandbrakeInput = false;
	PushControl();
}

void AExternalVehiclePawn::PushControl()
{
	float AuthoritativeSpeedMps = 0.0f;
	float StateAgeSeconds = 0.0f;
	SimCoreProtocol::FVehicleState State;
	const bool bHasFreshState = SimCoreClient->GetLatestState(State, StateAgeSeconds)
		&& StateAgeSeconds <= DirectionStateMaxAgeSeconds;
	if (bHasFreshState)
	{
		AuthoritativeSpeedMps = static_cast<float>(State.LinearVelocityBody.X);
	}

	float CommandThrottle = 0.0f;
	float CommandBrake = 0.0f;
	const bool bForwardPressed = ForwardPedalInput > KINDA_SMALL_NUMBER;
	const bool bReversePressed = ReversePedalInput > KINDA_SMALL_NUMBER;
	if (bForwardPressed && bReversePressed)
	{
		// Conflicting pedals can never create drive torque.
		CommandBrake = FMath::Max(ForwardPedalInput, ReversePedalInput);
	}
	else if (bForwardPressed)
	{
		if (SelectedGear == SimCoreProtocol::EVehicleGear::Drive)
		{
			// W remains usable with a stale state only when it cannot change gear.
			CommandThrottle = !bHasFreshState
				|| AuthoritativeSpeedMps >= -DirectionChangeSpeedMps
				? ForwardPedalInput
				: 0.0f;
			CommandBrake = CommandThrottle > 0.0f ? 0.0f : ForwardPedalInput;
		}
		else if (bHasFreshState
			&& AuthoritativeSpeedMps >= -DirectionChangeSpeedMps)
		{
			SelectedGear = SimCoreProtocol::EVehicleGear::Drive;
			CommandThrottle = ForwardPedalInput;
		}
		else
		{
			// While reversing, W is the service brake until nearly stopped.
			CommandBrake = ForwardPedalInput;
		}
	}
	else if (bReversePressed)
	{
		if (SelectedGear == SimCoreProtocol::EVehicleGear::Reverse)
		{
			CommandThrottle = !bHasFreshState
				|| AuthoritativeSpeedMps <= DirectionChangeSpeedMps
				? ReversePedalInput
				: 0.0f;
			CommandBrake = CommandThrottle > 0.0f ? 0.0f : ReversePedalInput;
		}
		else if (bHasFreshState
			&& AuthoritativeSpeedMps <= DirectionChangeSpeedMps)
		{
			SelectedGear = SimCoreProtocol::EVehicleGear::Reverse;
			CommandThrottle = ReversePedalInput;
		}
		else
		{
			// While moving forward, S is the service brake until nearly stopped.
			CommandBrake = ReversePedalInput;
		}
	}

	SimCoreClient->SetControl(
		CommandThrottle,
		CommandBrake,
		SteeringInput,
		bHandbrakeInput,
		SelectedGear);
}
