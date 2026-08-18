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
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(VehicleMesh);
	Camera->SetRelativeLocation(FVector(-500.0, 0.0, 250.0)); Camera->SetRelativeRotation(FRotator(-12.0, 0.0, 0.0));
	SimCoreClient = CreateDefaultSubobject<USimCoreClientComponent>(TEXT("SimCoreClient"));
}
void AExternalVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); SimCoreProtocol::FVehicleState State;
	if (!SimCoreClient->GetLatestState(State)) return;
	const FVector Target(State.NorthMeters * 100.0, State.EastMeters * 100.0, State.Altitude * 100.0 + 60.0);
	const FRotator Rotation(State.PitchDegrees, State.HeadingDegrees, State.RollDegrees);
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), Target, DeltaSeconds, PositionInterpolationSpeed));
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Rotation, DeltaSeconds, PositionInterpolationSpeed));
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
