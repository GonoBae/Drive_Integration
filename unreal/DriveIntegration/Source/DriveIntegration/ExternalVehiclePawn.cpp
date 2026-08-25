#include "ExternalVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "SimCoreClientComponent.h"
#include "SimCoreCoordinateFrames.h"
#include "SimCorePresentation.h"
#include "UObject/ConstructorHelpers.h"

AExternalVehiclePawn::AExternalVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	VehicleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleMesh"));
	SetRootComponent(VehicleMesh);
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		VehicleMesh->SetStaticMesh(Cube.Object);
		VehicleMesh->SetRelativeScale3D(FVector(2.2, 1.0, 0.6));
	}
	const FVector WheelLocations[4] = {
		FVector(135.0, -79.0, -55.0),
		FVector(135.0, 79.0, -55.0),
		FVector(-135.0, -79.0, -55.0),
		FVector(-135.0, 79.0, -55.0),
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(VehicleMesh);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Cube.Succeeded())
		{
			Wheel->SetStaticMesh(Cube.Object);
		}
		Wheel->SetRelativeLocation(WheelLocations[Index]);
		Wheel->SetRelativeScale3D(FVector(0.65, 0.22, 0.65));
		WheelMeshes.Add(Wheel);
	}
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VehicleMesh);
	Camera->SetRelativeLocation(FVector(-500.0, 0.0, 250.0));
	Camera->SetRelativeRotation(FRotator(-12.0, 0.0, 0.0));
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
		if (!WheelMeshes.IsValidIndex(WheelState.WheelIndex)) continue;
		const float AxleSpinDegrees = WheelState.WheelIndex < 2
			? FrontAxleSpinDegrees
			: RearAxleSpinDegrees;
		WheelMeshes[WheelState.WheelIndex]->SetRelativeRotation(
			SimCorePresentation::BuildWheelRelativeRotation(WheelState, AxleSpinDegrees));
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
	ThrottleInput = Value;
	PushControl();
}

void AExternalVehiclePawn::SetBrake(float Value)
{
	BrakeInput = Value;
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
	SimCoreClient->SetControl(
		ThrottleInput,
		BrakeInput,
		SteeringInput,
		bHandbrakeInput);
}
