#include "ExternalVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "SimCoreClientComponent.h"
#include "UObject/ConstructorHelpers.h"

AExternalVehiclePawn::AExternalVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true; AutoPossessPlayer = EAutoReceiveInput::Player0;
	VehicleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleMesh")); SetRootComponent(VehicleMesh);
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) { VehicleMesh->SetStaticMesh(Cube.Object); VehicleMesh->SetRelativeScale3D(FVector(2.2, 1.0, 0.6)); }
	const FVector WheelLocations[4] = {
		FVector(135.0, -79.0, -55.0), FVector(135.0, 79.0, -55.0),
		FVector(-135.0, -79.0, -55.0), FVector(-135.0, 79.0, -55.0)
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(VehicleMesh);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Cube.Succeeded()) Wheel->SetStaticMesh(Cube.Object);
		Wheel->SetRelativeLocation(WheelLocations[Index]);
		Wheel->SetRelativeScale3D(FVector(0.65, 0.22, 0.65));
		WheelMeshes.Add(Wheel);
	}
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(VehicleMesh);
	Camera->SetRelativeLocation(FVector(-500.0, 0.0, 250.0)); Camera->SetRelativeRotation(FRotator(-12.0, 0.0, 0.0));
	SimCoreClient = CreateDefaultSubobject<USimCoreClientComponent>(TEXT("SimCoreClient"));
}
void AExternalVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); SimCoreProtocol::FVehicleState State; float StateAgeSeconds = 0.0f;
	if (!SimCoreClient->GetLatestState(State, StateAgeSeconds)) return;
	const double PredictionSeconds = FMath::Min(static_cast<double>(StateAgeSeconds), static_cast<double>(MaxExtrapolationSeconds));
	const double HeadingRadians = FMath::DegreesToRadians(static_cast<double>(State.HeadingDegrees));
	const double ForwardSpeed = State.LinearVelocityBody.X;
	const double RightSpeed = State.LinearVelocityBody.Y;
	const double NorthVelocity = ForwardSpeed * FMath::Cos(HeadingRadians) - RightSpeed * FMath::Sin(HeadingRadians);
	const double EastVelocity = ForwardSpeed * FMath::Sin(HeadingRadians) + RightSpeed * FMath::Cos(HeadingRadians);
	const FVector Target(
		(State.NorthMeters + NorthVelocity * PredictionSeconds) * 100.0,
		(State.EastMeters + EastVelocity * PredictionSeconds) * 100.0,
		State.Altitude * 100.0 + 60.0);
	const FRotator Rotation(
		State.PitchDegrees + FMath::RadiansToDegrees(State.AngularVelocityBody.Y * PredictionSeconds),
		State.HeadingDegrees + FMath::RadiansToDegrees(State.YawRateRad * PredictionSeconds),
		State.RollDegrees + FMath::RadiansToDegrees(State.AngularVelocityBody.X * PredictionSeconds));
	SetActorLocationAndRotation(Target, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	float FrontAxleAngularSpeedRad = 0.0f;
	float RearAxleAngularSpeedRad = 0.0f;
	int32 FrontWheelCount = 0;
	int32 RearWheelCount = 0;
	for (const SimCoreProtocol::FVehicleState::FWheelState& WheelState : State.Wheels)
	{
		if (WheelState.WheelIndex < 2)
		{
			FrontAxleAngularSpeedRad += WheelState.AngularSpeedRad;
			++FrontWheelCount;
		}
		else if (WheelState.WheelIndex < 4)
		{
			RearAxleAngularSpeedRad += WheelState.AngularSpeedRad;
			++RearWheelCount;
		}
	}
	if (FrontWheelCount > 0) FrontAxleAngularSpeedRad /= FrontWheelCount;
	if (RearWheelCount > 0) RearAxleAngularSpeedRad /= RearWheelCount;
	if (FMath::Abs(State.SpeedMps) <= VisualWheelStopSpeedMps)
	{
		FrontAxleAngularSpeedRad = 0.0f;
		RearAxleAngularSpeedRad = 0.0f;
	}
	FrontAxleSpinDegrees = FMath::Fmod(
		FrontAxleSpinDegrees + FMath::RadiansToDegrees(FrontAxleAngularSpeedRad) * DeltaSeconds,
		360.0f);
	RearAxleSpinDegrees = FMath::Fmod(
		RearAxleSpinDegrees + FMath::RadiansToDegrees(RearAxleAngularSpeedRad) * DeltaSeconds,
		360.0f);

	for (const SimCoreProtocol::FVehicleState::FWheelState& WheelState : State.Wheels)
	{
		if (!WheelMeshes.IsValidIndex(WheelState.WheelIndex)) continue;
		const float AxleSpinDegrees = WheelState.WheelIndex < 2
			? FrontAxleSpinDegrees
			: RearAxleSpinDegrees;
		WheelMeshes[WheelState.WheelIndex]->SetRelativeRotation(FRotator(
			-AxleSpinDegrees,
			FMath::RadiansToDegrees(WheelState.SteeringAngleRad), 0.0f));
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
void AExternalVehiclePawn::SetThrottle(float V) { ThrottleInput = V; PushControl(); }
void AExternalVehiclePawn::SetBrake(float V) { BrakeInput = V; PushControl(); }
void AExternalVehiclePawn::SetSteering(float V) { SteeringInput = V; PushControl(); }
void AExternalVehiclePawn::SetHandbrakePressed() { bHandbrakeInput = true; PushControl(); }
void AExternalVehiclePawn::SetHandbrakeReleased() { bHandbrakeInput = false; PushControl(); }
void AExternalVehiclePawn::PushControl() { SimCoreClient->SetControl(ThrottleInput, BrakeInput, SteeringInput, bHandbrakeInput); }
