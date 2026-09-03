#include "ExternalVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "SimCoreClientComponent.h"
#include "SimCoreCoordinateFrames.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreDriveReplay.h"
#include "SimCoreExhaustComponent.h"
#include "SimCorePresentation.h"
#include "SimCoreSedanVisualContract.h"
#include "SimCoreSensorRig.h"
#include "SimCoreSteeringInput.h"
#include "SimCoreVehicleControlResolver.h"
#include "SimCoreVehicleAudio.h"
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
	// The editor generator authors centimeter-space meshes at the same CG and
	// wheel origins as SimCore. Never rescale the root or alter the physics rig.
	UStaticMesh* SedanBody = nullptr;
	UStaticMesh* SedanWheel = nullptr;
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::BodyPackagePath()))
	{
		SedanBody = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::BodyObjectPath());
	}
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::WheelPackagePath()))
	{
		SedanWheel = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::WheelObjectPath());
	}
	if (SedanBody)
	{
		VehicleMesh->SetStaticMesh(SedanBody);
		VehicleMesh->SetRelativeLocation(FVector::ZeroVector);
		VehicleMesh->SetRelativeScale3D(FVector::OneVector);
	}
	else if (Cube.Succeeded())
	{
		VehicleMesh->SetStaticMesh(Cube.Object);
		// Match the authoritative sedan envelope while leaving the root unscaled.
		VehicleMesh->SetRelativeLocation(FVector(-13.5, 0.0, 0.0));
		VehicleMesh->SetRelativeScale3D(FVector(4.3, 1.8, 0.6));
	}
	const TConstArrayView<FVector> WheelOrigins =
		SimCoreSedanVisualContract::WheelOriginsCm();
	for (int32 Index = 0; Index < WheelOrigins.Num(); ++Index)
	{
		SuspensionMountLocationsCm[Index] = FVector(
			WheelOrigins[Index].X, WheelOrigins[Index].Y, 0.0);
		USceneComponent* WheelPivot = CreateDefaultSubobject<USceneComponent>(
			*FString::Printf(TEXT("WheelPivot%d"), Index));
		WheelPivot->SetupAttachment(PresentationRoot);
		WheelPivot->SetRelativeLocation(WheelOrigins[Index]);
		WheelPivots.Add(WheelPivot);

		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(WheelPivot);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (SedanWheel)
		{
			Wheel->SetStaticMesh(SedanWheel);
			Wheel->SetRelativeScale3D(FVector::OneVector);
		}
		else if (Cube.Succeeded())
		{
			Wheel->SetStaticMesh(Cube.Object);
			Wheel->SetRelativeScale3D(FVector(0.64, 0.22, 0.64));
		}
		WheelMeshes.Add(Wheel);
	}
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(PresentationRoot);
	// TargetOffset is world-space: a banked chassis cannot swing the orbit
	// center sideways or make the horizon tilt on a slope.
	CameraBoom->TargetOffset = FVector(0.0, 0.0, 145.0);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(SimCoreOrbitCamera::DefaultPitchDegrees, 0.0, 0.0));
	CameraBoom->TargetArmLength = SimCoreOrbitCamera::DefaultDistanceCm;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 15.0f;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->AddTickPrerequisiteActor(this);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	SimCoreClient = CreateDefaultSubobject<USimCoreClientComponent>(TEXT("SimCoreClient"));
	VehicleAudio = CreateDefaultSubobject<USimCoreVehicleAudioComponent>(TEXT("VehicleAudio"));
	VehicleAudio->SetupAttachment(PresentationRoot);
	ExhaustEffect = CreateDefaultSubobject<USimCoreExhaustComponent>(TEXT("ExhaustEffect"));
	ExhaustEffect->SetupAttachment(PresentationRoot);
	// The authored sedan's tailpipe is behind the rear axle on the right.
	ExhaustEffect->SetRelativeLocation(FVector(-221.0, 55.0, -25.0));
	DriveReplay = CreateDefaultSubobject<USimCoreDriveReplayComponent>(TEXT("DriveReplay"));
	SensorRig = CreateDefaultSubobject<USimCoreSensorRigComponent>(TEXT("SensorRig"));
	SensorRig->SetupAttachment(PresentationRoot);
}

void AExternalVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	InitializeDamagePresentation();
	VehicleAudio->Silence();
	VehicleAudio->Start();
}

void AExternalVehiclePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	VehicleAudio->Silence();
	VehicleAudio->Stop();
	Super::EndPlay(EndPlayReason);
}

void AExternalVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSteeringInput(DeltaSeconds);

	SimCoreProtocol::FVehicleState State;
	float StateAgeSeconds = 0.0f;
	if (!SimCoreClient->GetLatestState(State, StateAgeSeconds))
	{
		VehicleAudio->Silence();
		ExhaustEffect->ApplyUnavailableState(DeltaSeconds);
		// Camera input remains useful while disconnected or awaiting reset; it
		// must not depend on, or send, an authoritative vehicle control command.
		UpdateOrbitCamera(DeltaSeconds);
		return;
	}
	VehicleAudio->SetAuthoritativeState(State, StateAgeSeconds, StateStaleTimeoutSeconds);
	ExhaustEffect->ApplyAuthoritativeState(
		State, StateAgeSeconds, StateStaleTimeoutSeconds, DeltaSeconds);
	DriveReplay->CaptureAuthoritativeState(State);
	SensorRig->ObserveAuthoritativeState(State);

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
	ApplyDamagePresentation(State, StateAgeSeconds);

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
		else
		{
			// Missing terrain contact freezes the supported centre, not the
			// authoritative steering rack. Without a contact plane, steer about
			// body up until valid contact supplies the ground-aligned pivot again.
			WheelPivots[WheelState.WheelIndex]->SetRelativeRotation(
				SimCorePresentation::BuildWheelPivotRelativeRotation(
					WheelState, FVector::UpVector));
		}
		WheelMeshes[WheelState.WheelIndex]->SetRelativeRotation(
			SimCorePresentation::BuildWheelSpinRelativeRotation(AxleSpinDegrees));
	}
	DrawVehicleDebug(State);
	UpdateOrbitCamera(DeltaSeconds);
}

void AExternalVehiclePawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	Input->BindAxis(TEXT("Throttle"), this, &AExternalVehiclePawn::SetThrottle);
	Input->BindAxis(TEXT("Brake"), this, &AExternalVehiclePawn::SetBrake);
	Input->BindAxis(TEXT("Steering"), this, &AExternalVehiclePawn::SetSteering);
	Input->BindAction(TEXT("SteerLeft"), IE_Pressed, this, &AExternalVehiclePawn::SetSteerLeftPressed);
	Input->BindAction(TEXT("SteerLeft"), IE_Released, this, &AExternalVehiclePawn::SetSteerLeftReleased);
	Input->BindAction(TEXT("SteerRight"), IE_Pressed, this, &AExternalVehiclePawn::SetSteerRightPressed);
	Input->BindAction(TEXT("SteerRight"), IE_Released, this, &AExternalVehiclePawn::SetSteerRightReleased);
	Input->BindAction(TEXT("Handbrake"), IE_Pressed, this, &AExternalVehiclePawn::SetSideBrakePressed);
	Input->BindAction(TEXT("Handbrake"), IE_Released, this, &AExternalVehiclePawn::SetSideBrakeReleased);
	Input->BindAxis(TEXT("CameraOrbitYaw"), this, &AExternalVehiclePawn::OrbitCameraYaw);
	Input->BindAxis(TEXT("CameraOrbitPitch"), this, &AExternalVehiclePawn::OrbitCameraPitch);
	Input->BindAxis(TEXT("CameraGamepadYaw"), this, &AExternalVehiclePawn::SetCameraGamepadYaw);
	Input->BindAxis(TEXT("CameraGamepadPitch"), this, &AExternalVehiclePawn::SetCameraGamepadPitch);
	Input->BindAxis(TEXT("CameraZoom"), this, &AExternalVehiclePawn::ZoomCamera);
	Input->BindAction(TEXT("CameraReset"), IE_Pressed, this, &AExternalVehiclePawn::ResetCameraView);
	Input->BindAction(TEXT("VehicleDebug"), IE_Pressed, this, &AExternalVehiclePawn::ToggleVehicleDebug);
	Input->BindAction(TEXT("DriveRecord"), IE_Pressed, DriveReplay.Get(),
		&USimCoreDriveReplayComponent::ToggleRecording);
	Input->BindAction(TEXT("DriveReplay"), IE_Pressed, DriveReplay.Get(),
		&USimCoreDriveReplayComponent::ToggleReplay);
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
}

void AExternalVehiclePawn::OrbitCameraYaw(float Value)
{
	SimCoreOrbitCamera::AddMouseDelta(OrbitCameraState, Value, 0.0f, CameraMouseDegreesPerInputUnit);
}

void AExternalVehiclePawn::OrbitCameraPitch(float Value)
{
	SimCoreOrbitCamera::AddMouseDelta(OrbitCameraState, 0.0f, Value, CameraMouseDegreesPerInputUnit);
}

void AExternalVehiclePawn::SetCameraGamepadYaw(float Value)
{
	CameraGamepadYaw = Value;
}

void AExternalVehiclePawn::SetCameraGamepadPitch(float Value)
{
	CameraGamepadPitch = Value;
}

void AExternalVehiclePawn::ZoomCamera(float Value)
{
	SimCoreOrbitCamera::AddZoom(OrbitCameraState, Value, CameraZoomStepCm);
}

void AExternalVehiclePawn::ResetCameraView()
{
	SimCoreOrbitCamera::ResetView(OrbitCameraState);
}

void AExternalVehiclePawn::ToggleVehicleDebug()
{
	bVehicleDebugEnabled = !bVehicleDebugEnabled;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 2.0f, bVehicleDebugEnabled ? FColor::Green : FColor::Silver,
			bVehicleDebugEnabled
				? TEXT("Vehicle physics debug: ON")
				: TEXT("Vehicle physics debug: OFF"));
	}
}

void AExternalVehiclePawn::DrawVehicleDebug(
	const SimCoreProtocol::FVehicleState& State) const
{
	if (!bVehicleDebugEnabled || !GetWorld()) return;
	DrawDebugString(
		GetWorld(), GetActorLocation() + GetActorUpVector() * 180.0f,
		FString::Printf(TEXT("DMG %.1f%% impact %.0f N s event %u"),
			State.DamagePercent, State.LastImpactImpulseNs,
			State.CollisionEventSequence),
		nullptr, FColor::Magenta, 0.0f, false, 1.1f);

	if (State.CollisionHalfLengthMeters > 0.0f
		&& State.CollisionHalfWidthMeters > 0.0f
		&& State.CollisionHalfHeightMeters > 0.0f)
	{
		DrawDebugBox(
			GetWorld(), GetActorLocation(),
			FVector(
				State.CollisionHalfLengthMeters,
				State.CollisionHalfWidthMeters,
				State.CollisionHalfHeightMeters) * 100.0f,
			GetActorQuat(), FColor::Cyan, false, -1.0f, 0, 2.0f);
	}

	const FVector VelocityWorldCmPerSecond(
		SimCoreCoordinateFrames::MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
			State.LinearVelocityEnu));
	DrawDebugDirectionalArrow(
		GetWorld(), GetActorLocation(),
		GetActorLocation() + VelocityWorldCmPerSecond * 0.25f,
		20.0f, FColor::Green, false, -1.0f, 0, 2.0f);

	for (const SimCoreProtocol::FVehicleState::FWheelState& Wheel : State.Wheels)
	{
		if (Wheel.WheelIndex >= 4 || !WheelPivots.IsValidIndex(Wheel.WheelIndex)) continue;

		const FVector MountWorld = GetActorTransform().TransformPosition(
			SuspensionMountLocationsCm[Wheel.WheelIndex]);
		FVector ContactWorld = MountWorld - GetActorUpVector() * DebugSuspensionRayLengthCm;
		FVector ContactNormalWorld = GetActorUpVector();
		if (Wheel.bInContact)
		{
			ContactWorld = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(
				Wheel.ContactPointEnu, VisualPositionOffsetCm);
			ContactNormalWorld = FVector(
				SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(
					Wheel.ContactNormalEnu)).GetSafeNormal(
						UE_SMALL_NUMBER, GetActorUpVector());
		}

		const FColor ContactColor = Wheel.bInContact ? FColor::Green : FColor::Red;
		DrawDebugLine(GetWorld(), MountWorld, ContactWorld,
			ContactColor, false, -1.0f, 0, 2.0f);
		DrawDebugSphere(GetWorld(), ContactWorld, 7.0f, 8,
			ContactColor, false, -1.0f, 0, 1.5f);
		DrawDebugDirectionalArrow(
			GetWorld(), ContactWorld,
			ContactWorld + ContactNormalWorld * DebugContactNormalLengthCm,
			12.0f, FColor::Yellow, false, -1.0f, 0, 1.5f);

		if (Wheel.bInContact)
		{
			const FVector LongitudinalDirection =
				WheelPivots[Wheel.WheelIndex]->GetForwardVector().GetSafeNormal();
			// Protocol lateral force is FLU left-positive; Unreal component Y is right-positive.
			const FVector LateralDirection =
				-WheelPivots[Wheel.WheelIndex]->GetRightVector().GetSafeNormal();
			DrawDebugDirectionalArrow(
				GetWorld(), ContactWorld,
				ContactWorld + LongitudinalDirection
					* Wheel.LongitudinalForceN * DebugForceScaleCmPerNewton,
				12.0f, FColor::Orange, false, -1.0f, 0, 2.0f);
			DrawDebugDirectionalArrow(
				GetWorld(), ContactWorld,
				ContactWorld + LateralDirection
					* Wheel.LateralForceN * DebugForceScaleCmPerNewton,
				12.0f, FColor::Blue, false, -1.0f, 0, 2.0f);
		}

		DrawDebugString(
			GetWorld(), ContactWorld + ContactNormalWorld * 20.0f,
			FString::Printf(TEXT("W%u Fz %.0fN slip %.2f / %.1fdeg"),
				Wheel.WheelIndex, Wheel.NormalLoadN, Wheel.LongitudinalSlip,
				FMath::RadiansToDegrees(Wheel.SlipAngleRad)),
			nullptr, ContactColor, 0.0f, false, 1.0f);
	}
}

void AExternalVehiclePawn::ApplyDamagePresentation(
	const SimCoreProtocol::FVehicleState& State,
	const float StateAgeSeconds)
{
	if (SimCoreDamagePresentation::Advance(DamagePresentationAccumulator, State))
	{
		ApplyDentMaterialParameters(DamagePresentationAccumulator.Weights);
	}

	if (State.CollisionEventSequence == LastPresentedCollisionEventSequence)
	{
		return;
	}
	LastPresentedCollisionEventSequence = State.CollisionEventSequence;
	if (State.CollisionEventSequence > 0
		&& StateAgeSeconds <= StateStaleTimeoutSeconds
		&& GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 2.0f, FColor::Orange,
			FString::Printf(TEXT("IMPACT %.0f N s  DAMAGE %.1f%%"),
				State.LastImpactImpulseNs, State.DamagePercent));
	}
}

void AExternalVehiclePawn::InitializeDamagePresentation()
{
	DamagePresentationAccumulator = {};
	DamageMaterialInstances.Reset();
	for (int32 MaterialIndex = 0;
		MaterialIndex < VehicleMesh->GetNumMaterials();
		++MaterialIndex)
	{
		if (UMaterialInstanceDynamic* Material =
			VehicleMesh->CreateDynamicMaterialInstance(MaterialIndex))
		{
			DamageMaterialInstances.Add(Material);
		}
	}
	ApplyDentMaterialParameters({});
}

void AExternalVehiclePawn::ApplyDentMaterialParameters(
	const SimCoreDamagePresentation::FZoneWeights& Weights)
{
	constexpr SimCoreProtocol::EVehicleDamageZone Zones[] = {
		SimCoreProtocol::EVehicleDamageZone::Front,
		SimCoreProtocol::EVehicleDamageZone::Rear,
		SimCoreProtocol::EVehicleDamageZone::Left,
		SimCoreProtocol::EVehicleDamageZone::Right,
		SimCoreProtocol::EVehicleDamageZone::Roof,
		SimCoreProtocol::EVehicleDamageZone::Underbody,
	};
	for (UMaterialInstanceDynamic* Material : DamageMaterialInstances)
	{
		if (!Material)
		{
			continue;
		}
		for (const SimCoreProtocol::EVehicleDamageZone Zone : Zones)
		{
			Material->SetScalarParameterValue(
				SimCoreDamagePresentation::MaterialParameterName(Zone),
				Weights.Get(Zone));
		}
	}
}

void AExternalVehiclePawn::UpdateOrbitCamera(float DeltaSeconds)
{
	SimCoreOrbitCamera::AddGamepadRate(OrbitCameraState, CameraGamepadYaw,
		CameraGamepadPitch, CameraGamepadDegreesPerSecond, DeltaSeconds);
	CameraBoom->TargetArmLength = OrbitCameraState.DistanceCm;
	CameraBoom->SetWorldRotation(SimCoreOrbitCamera::BuildWorldRotation(
		OrbitCameraState, static_cast<float>(GetActorRotation().Yaw)));
}

void AExternalVehiclePawn::UpdateSteeringInput(const float DeltaSeconds)
{
	KeyboardSteeringInput = SimCoreSteeringInput::AdvanceKeyboardCommand(
		KeyboardSteeringInput,
		bSteerLeftPressed,
		bSteerRightPressed,
		DeltaSeconds,
		KeyboardSteeringRiseRatePerSecond,
		KeyboardSteeringReturnRatePerSecond);
	const bool bKeyboardOwnsInput = bSteerLeftPressed || bSteerRightPressed
		|| !FMath::IsNearlyZero(KeyboardSteeringInput, 1.0e-4f);
	const float Resolved = SimCoreSteeringInput::ResolveCommand(
		KeyboardSteeringInput, bKeyboardOwnsInput, AnalogSteeringInput);
	if (!FMath::IsNearlyEqual(Resolved, SteeringInput, 1.0e-4f))
	{
		SteeringInput = Resolved;
		PushControl();
	}
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
	AnalogSteeringInput = SimCoreCoordinateFrames::InputAxisToCanonicalSteering(Value);
	UpdateSteeringInput(0.0f);
}

void AExternalVehiclePawn::SetSteerLeftPressed() { bSteerLeftPressed = true; }
void AExternalVehiclePawn::SetSteerLeftReleased() { bSteerLeftPressed = false; }
void AExternalVehiclePawn::SetSteerRightPressed() { bSteerRightPressed = true; }
void AExternalVehiclePawn::SetSteerRightReleased() { bSteerRightPressed = false; }

void AExternalVehiclePawn::SetSideBrakePressed()
{
	bSideBrakeInput = true;
	PushControl();
}

void AExternalVehiclePawn::SetSideBrakeReleased()
{
	bSideBrakeInput = false;
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

	SimCoreVehicleControlResolver::FInput Input;
	Input.ForwardPedal = ForwardPedalInput;
	Input.ReversePedal = ReversePedalInput;
	Input.Steering = SteeringInput;
	Input.bSideBrake = bSideBrakeInput;
	Input.SelectedGear = SelectedGear;
	Input.bHasFreshAuthoritativeState = bHasFreshState;
	Input.AuthoritativeLongitudinalSpeedMps = AuthoritativeSpeedMps;
	Input.DirectionChangeSpeedMps = DirectionChangeSpeedMps;
	const SimCoreVehicleControlResolver::FOutput Output =
		SimCoreVehicleControlResolver::Resolve(Input);
	SelectedGear = Output.Gear;

	SimCoreClient->SetControl(
		Output.Throttle,
		Output.Brake,
		Output.Steering,
		Output.bSideBrake,
		Output.Gear);
}
