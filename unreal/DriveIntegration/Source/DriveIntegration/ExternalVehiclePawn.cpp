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
#include "SimCoreDeformableBody.h"
#include "SimCoreDriverPresentation.h"
#include "SimCoreTurnSignals.h"
#include "SimCoreDriveReplay.h"
#include "SimCoreExhaustComponent.h"
#include "SimCorePresentation.h"
#include "SimCoreSedanVisualContract.h"
#include "SimCoreSensorRig.h"
#include "SimCoreSteeringInput.h"
#include "SimCoreVehicleControlResolver.h"
#include "SimCoreVehicleAudio.h"
#include "SimCoreVehicleHorn.h"
#include "SimCoreVehicleVisualProfile.h"
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
	DeformableBody = CreateDefaultSubobject<USimCoreDeformableBody>(TEXT("DeformableBody"));
	DeformableBody->SetupAttachment(PresentationRoot);
	TurnSignals = CreateDefaultSubobject<USimCoreTurnSignals>(TEXT("TurnSignals"));
	TurnSignals->SetupAttachment(PresentationRoot);
	TurnSignals->BindBodies(VehicleMesh, DeformableBody);
	DriverPresentation = CreateDefaultSubobject<USimCoreDriverPresentation>(TEXT("DriverPresentation"));
	DriverPresentation->SetupAttachment(PresentationRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	// The editor generator authors centimeter-space meshes at the same CG and
	// wheel origins as SimCore. Never rescale the root or alter the physics rig.
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::BodyPackagePath()))
	{
		SedanBodyMesh = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::BodyObjectPath());
	}
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::WheelPackagePath()))
	{
		SharedWheelMesh = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::WheelObjectPath());
	}
	auto LoadFleetMesh = [](const TCHAR* Name) -> UStaticMesh*
	{
		const FString PackagePath = FString(TEXT("/Game/Vehicles/NpcFleet/")) + Name;
		return FPackageName::DoesPackageExist(PackagePath)
			? LoadObject<UStaticMesh>(nullptr, *(PackagePath + TEXT(".") + Name))
			: nullptr;
	};
	CompactBodyMesh = LoadFleetMesh(TEXT("SM_CompactBody"));
	TruckBodyMesh = LoadFleetMesh(TEXT("SM_TruckBody"));
	MotorcycleBodyMesh = LoadFleetMesh(TEXT("SM_MotorcycleBody"));
	if (SedanBodyMesh)
	{
		VehicleMesh->SetStaticMesh(SedanBodyMesh);
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
	DriverPresentation->SetPresentationEnabled(SedanBodyMesh != nullptr);
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
		if (SharedWheelMesh)
		{
			Wheel->SetStaticMesh(SharedWheelMesh);
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
	VehicleHorn = CreateDefaultSubobject<USimCoreVehicleHornComponent>(TEXT("VehicleHorn"));
	VehicleHorn->SetupAttachment(PresentationRoot);
	VehicleHorn->SetRelativeLocation(FVector(185.0, 0.0, 20.0));
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
	VehicleHorn->ResetHornState();
	VehicleHorn->Start();
}

void AExternalVehiclePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	VehicleAudio->Silence();
	VehicleAudio->Stop();
	VehicleHorn->ResetHornState();
	VehicleHorn->Stop();
	Super::EndPlay(EndPlayReason);
}

void AExternalVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSteeringInput(DeltaSeconds);
	TurnSignals->UpdateSignal(ManualIndicator, GetWorld()->GetTimeSeconds(), bHazardLights);

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
	ConfigureVehicleClass(State.RuntimeVehicleClass);
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
	DriverPresentation->ApplyAuthoritativeState(State);

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
	Input->BindAction(TEXT("CameraCycle"), IE_Pressed, this, &AExternalVehiclePawn::CycleCameraView);
	Input->BindAction(TEXT("VehicleDebug"), IE_Pressed, this, &AExternalVehiclePawn::ToggleVehicleDebug);
	Input->BindAction(TEXT("LeftIndicator"), IE_Pressed, this, &AExternalVehiclePawn::ToggleLeftIndicator);
	Input->BindAction(TEXT("RightIndicator"), IE_Pressed, this, &AExternalVehiclePawn::ToggleRightIndicator);
	Input->BindAction(TEXT("HazardLights"), IE_Pressed, this, &AExternalVehiclePawn::ToggleHazardLights);
	Input->BindAction(TEXT("Horn"), IE_Pressed, VehicleHorn.Get(),
		&USimCoreVehicleHornComponent::HandleHornInput);
	Input->BindAction(TEXT("SelectSedan"), IE_Pressed, this,
		&AExternalVehiclePawn::SelectSedan);
	Input->BindAction(TEXT("SelectCompact"), IE_Pressed, this,
		&AExternalVehiclePawn::SelectCompact);
	Input->BindAction(TEXT("SelectTruck"), IE_Pressed, this,
		&AExternalVehiclePawn::SelectTruck);
	Input->BindAction(TEXT("SelectMotorcycle"), IE_Pressed, this,
		&AExternalVehiclePawn::SelectMotorcycle);
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

namespace
{
const TCHAR* PlayerVehicleClassName(
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	switch (VehicleClass)
	{
	case SimCoreProtocol::ERuntimeVehicleClass::Sedan: return TEXT("SEDAN");
	case SimCoreProtocol::ERuntimeVehicleClass::Compact: return TEXT("COMPACT");
	case SimCoreProtocol::ERuntimeVehicleClass::Truck: return TEXT("TRUCK");
	case SimCoreProtocol::ERuntimeVehicleClass::Motorcycle: return TEXT("MOTORCYCLE");
	default: return TEXT("UNKNOWN");
	}
}
}

void AExternalVehiclePawn::SelectPlayerVehicleClass(
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	if (!SimCoreClient || !SimCoreClient->SelectVehicleClass(VehicleClass))
	{
		return;
	}
	ForwardPedalInput = 0.0f;
	ReversePedalInput = 0.0f;
	AnalogSteeringInput = 0.0f;
	KeyboardSteeringInput = 0.0f;
	SteeringInput = 0.0f;
	bSideBrakeInput = false;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("VEHICLE: %s - resetting at spawn (1/2/3/4)"),
				PlayerVehicleClassName(VehicleClass)));
	}
}

void AExternalVehiclePawn::SelectSedan()
{
	SelectPlayerVehicleClass(SimCoreProtocol::ERuntimeVehicleClass::Sedan);
}

void AExternalVehiclePawn::SelectCompact()
{
	SelectPlayerVehicleClass(SimCoreProtocol::ERuntimeVehicleClass::Compact);
}

void AExternalVehiclePawn::SelectTruck()
{
	SelectPlayerVehicleClass(SimCoreProtocol::ERuntimeVehicleClass::Truck);
}

void AExternalVehiclePawn::SelectMotorcycle()
{
	SelectPlayerVehicleClass(SimCoreProtocol::ERuntimeVehicleClass::Motorcycle);
}

int32 AExternalVehiclePawn::GetVisibleWheelCount() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent* Wheel : WheelMeshes)
	{
		Count += Wheel && Wheel->IsVisible() ? 1 : 0;
	}
	return Count;
}

bool AExternalVehiclePawn::ConfigureVehicleClass(
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Unspecified)
	{
		VehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	}
	UStaticMesh* BodyMesh = nullptr;
	switch (VehicleClass)
	{
	case SimCoreProtocol::ERuntimeVehicleClass::Sedan: BodyMesh = SedanBodyMesh; break;
	case SimCoreProtocol::ERuntimeVehicleClass::Compact: BodyMesh = CompactBodyMesh; break;
	case SimCoreProtocol::ERuntimeVehicleClass::Truck: BodyMesh = TruckBodyMesh; break;
	case SimCoreProtocol::ERuntimeVehicleClass::Motorcycle: BodyMesh = MotorcycleBodyMesh; break;
	default: return false;
	}
	if (!BodyMesh || !SharedWheelMesh || WheelMeshes.Num() != 4 || WheelPivots.Num() != 4)
	{
		return false;
	}
	if (DisplayedVehicleClass == VehicleClass && VehicleMesh->GetStaticMesh() == BodyMesh)
	{
		return true;
	}

	SimCoreVehicleVisualProfile::FProfile Profile;
	if (!SimCoreVehicleVisualProfile::Resolve(VehicleClass, Profile)) return false;

	DeformableBody->ResetDeformation();
	VehicleMesh->EmptyOverrideMaterials();
	VehicleMesh->SetStaticMesh(BodyMesh);
	VehicleMesh->SetRelativeTransform(FTransform::Identity);
	VehicleMesh->SetVisibility(true);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		WheelPivots[Index]->SetRelativeLocation(Profile.WheelOriginsCm[Index]);
		WheelPivots[Index]->SetRelativeRotation(FRotator::ZeroRotator);
		WheelMeshes[Index]->SetStaticMesh(SharedWheelMesh);
		WheelMeshes[Index]->SetRelativeScale3D(Profile.WheelScales[Index]);
		WheelMeshes[Index]->SetRelativeRotation(FRotator::ZeroRotator);
		WheelMeshes[Index]->SetVisibility(Profile.IsWheelVisible(Index));
		SuspensionMountLocationsCm[Index] = FVector(
			Profile.WheelOriginsCm[Index].X, Profile.WheelOriginsCm[Index].Y, 0.0);
	}
	VisualTireRadiusMeters = static_cast<float>(
		SharedWheelMesh->GetBoundingBox().GetExtent().Z * Profile.WheelScales[0].Z * 0.01);
	TurnSignals->SetLampPositions(MakeArrayView(Profile.LampLocationsCm));
	DriverPresentation->ConfigureVehicleClass(VehicleClass);
	VehicleHorn->SetRelativeLocation(FVector(
		BodyMesh->GetBoundingBox().Max.X - 18.0, 0.0,
		FMath::Clamp(BodyMesh->GetBoundingBox().GetCenter().Z, 18.0, 80.0)));
	ExhaustEffect->SetRelativeLocation(Profile.ExhaustLocationCm);
	DamagePresentationAccumulator = {};
	LastPresentedCollisionEventSequence = 0;
	InitializeDamagePresentation();
	DisplayedVehicleClass = VehicleClass;
	return true;
}

void AExternalVehiclePawn::OrbitCameraYaw(float Value)
{
	if (CameraMode == SimCoreOrbitCamera::EMode::Follow)
	{
		SimCoreOrbitCamera::AddMouseDelta(OrbitCameraState, Value, 0.0f, CameraMouseDegreesPerInputUnit);
	}
	else if (CameraMode == SimCoreOrbitCamera::EMode::Driver)
	{
		SimCoreOrbitCamera::AddDriverMouseDelta(DriverCameraLook, Value, 0.0f, CameraMouseDegreesPerInputUnit);
	}
}

void AExternalVehiclePawn::OrbitCameraPitch(float Value)
{
	if (CameraMode == SimCoreOrbitCamera::EMode::Follow)
	{
		SimCoreOrbitCamera::AddMouseDelta(OrbitCameraState, 0.0f, Value, CameraMouseDegreesPerInputUnit);
	}
	else if (CameraMode == SimCoreOrbitCamera::EMode::Driver)
	{
		SimCoreOrbitCamera::AddDriverMouseDelta(DriverCameraLook, 0.0f, Value, CameraMouseDegreesPerInputUnit);
	}
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
	if (CameraMode == SimCoreOrbitCamera::EMode::Follow)
	{
		SimCoreOrbitCamera::AddZoom(OrbitCameraState, Value, CameraZoomStepCm);
	}
}

void AExternalVehiclePawn::ResetCameraView()
{
	SimCoreOrbitCamera::ResetView(OrbitCameraState);
	DriverCameraLook = {};
	CameraMode = SimCoreOrbitCamera::EMode::Follow;
	UpdateOrbitCamera(0.0f);
}

void AExternalVehiclePawn::CycleCameraView()
{
	CameraMode = SimCoreOrbitCamera::NextMode(CameraMode);
	UpdateOrbitCamera(0.0f);
	if (GEngine)
	{
		const bool bCabFront = SimCoreOrbitCamera::GetVehicleMounts(
			DisplayedVehicleClass).bDriverUsesCabFrontFallback;
		const TCHAR* Label = CameraMode == SimCoreOrbitCamera::EMode::Follow
			? TEXT("CAMERA: FOLLOW (mouse / right stick, wheel zoom)")
			: CameraMode == SimCoreOrbitCamera::EMode::FixedRear
				? TEXT("CAMERA: FIXED REAR")
				: bCabFront ? TEXT("CAMERA: CAB FRONT (truck interior unavailable)")
					: TEXT("CAMERA: DRIVER (mouse / right stick to look)");
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Silver, Label);
	}
}

void AExternalVehiclePawn::ToggleVehicleDebug()
{
	bVehicleDebugEnabled = !bVehicleDebugEnabled;
	if (SimCoreClient) SimCoreClient->bShowDebugHud = bVehicleDebugEnabled;
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
	if (SimCoreClient) SimCoreClient->DrawRuntimeEntityDebug();
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
		SimCoreVehicleVisualProfile::FProfile Profile;
		SimCoreVehicleVisualProfile::Resolve(State.RuntimeVehicleClass, Profile);
		// The planar solver uses a vertical prism above the ground, not an OBB
		// centred on the visual CG. Pitch/roll is handled by the chassis shell.
		const FVector CollisionCentre = GetActorLocation() + FVector::UpVector
			* (0.10f + State.CollisionHalfHeightMeters - Profile.CgHeightMeters) * 100.0f;
		DrawDebugBox(
			GetWorld(), CollisionCentre,
			FVector(
				State.CollisionHalfLengthMeters,
				State.CollisionHalfWidthMeters,
				State.CollisionHalfHeightMeters) * 100.0f,
			FRotator(0.0, GetActorRotation().Yaw, 0.0).Quaternion(),
			FColor::Cyan, false, -1.0f, 0, 2.0f);
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
		DeformableBody->ApplyDamage(VehicleMesh, DamagePresentationAccumulator.Weights);
		TurnSignals->SetBodyDamage(DamagePresentationAccumulator.Weights);
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
	VehicleMesh->SetEvaluateWorldPositionOffset(true);
	VehicleMesh->SetWorldPositionOffsetDisableDistance(0);
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
				Weights.bContactLocal ? 0.0f : Weights.Get(Zone));
		}
	}
	if (DriverPresentation)
	{
		DriverPresentation->ApplyDoorDamage(Weights);
	}
}

void AExternalVehiclePawn::UpdateOrbitCamera(float DeltaSeconds)
{
	using namespace SimCoreOrbitCamera;
	const FVehicleMounts Mounts = GetVehicleMounts(DisplayedVehicleClass);
	const bool bDriver = CameraMode == EMode::Driver;
	DriverPresentation->SetDriverViewActive(bDriver);
	CameraBoom->bDoCollisionTest = !bDriver;
	Camera->SetFieldOfView(bDriver ? 95.0f : 90.0f);
	if (CameraMode == EMode::Follow)
	{
		AddGamepadRate(OrbitCameraState, CameraGamepadYaw,
			CameraGamepadPitch, CameraGamepadDegreesPerSecond, DeltaSeconds);
		CameraBoom->TargetOffset = FVector(0.0, 0.0, Mounts.FixedTargetHeightCm);
		CameraBoom->TargetArmLength = OrbitCameraState.DistanceCm;
		CameraBoom->SetWorldRotation(BuildWorldRotation(
			OrbitCameraState, static_cast<float>(GetActorRotation().Yaw)));
	}
	else if (bDriver)
	{
		AddDriverGamepadRate(DriverCameraLook, CameraGamepadYaw,
			CameraGamepadPitch, CameraGamepadDegreesPerSecond, DeltaSeconds);
		// Zero-length boom deliberately has no scenery pull-in: this camera is
		// mounted in the car, not swept from a chase target through the cabin.
		FVector DriverMount = Mounts.DriverLocationCm;
		if (!SedanBodyMesh && DisplayedVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Unspecified)
		{
			DriverMount = FVector(215.0, -34.0, 82.0);
		}
		CameraBoom->TargetOffset = GetActorQuat().RotateVector(DriverMount);
		CameraBoom->TargetArmLength = 0.0f;
		CameraBoom->SetWorldRotation(BuildBodyRelativeRotation(DriverCameraLook, GetActorQuat()));
	}
	else
	{
		CameraBoom->TargetOffset = GetActorQuat().RotateVector(FVector(0.0, 0.0, Mounts.FixedTargetHeightCm));
		CameraBoom->TargetArmLength = Mounts.FixedDistanceCm;
		FDriverLook FixedLook;
		FixedLook.PitchDegrees = -12.0f;
		CameraBoom->SetWorldRotation(BuildBodyRelativeRotation(FixedLook, GetActorQuat()));
	}
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
void AExternalVehiclePawn::ToggleLeftIndicator()
{
	bHazardLights = false;
	ManualIndicator = ManualIndicator == SimCoreProtocol::ETurnIndicator::Left
		? SimCoreProtocol::ETurnIndicator::Off : SimCoreProtocol::ETurnIndicator::Left;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f,
			ManualIndicator == SimCoreProtocol::ETurnIndicator::Left
				? FColor(255, 170, 24) : FColor::Silver,
			ManualIndicator == SimCoreProtocol::ETurnIndicator::Left
				? TEXT("LEFT INDICATOR: ON (Q)") : TEXT("INDICATORS: OFF"));
	}
}
void AExternalVehiclePawn::ToggleRightIndicator()
{
	bHazardLights = false;
	ManualIndicator = ManualIndicator == SimCoreProtocol::ETurnIndicator::Right
		? SimCoreProtocol::ETurnIndicator::Off : SimCoreProtocol::ETurnIndicator::Right;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f,
			ManualIndicator == SimCoreProtocol::ETurnIndicator::Right
				? FColor(255, 170, 24) : FColor::Silver,
			ManualIndicator == SimCoreProtocol::ETurnIndicator::Right
				? TEXT("RIGHT INDICATOR: ON (E)") : TEXT("INDICATORS: OFF"));
	}
}
void AExternalVehiclePawn::ToggleHazardLights()
{
	bHazardLights = !bHazardLights;
	ManualIndicator = SimCoreProtocol::ETurnIndicator::Off;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f,
			bHazardLights ? FColor(255, 70, 35) : FColor::Silver,
			bHazardLights ? TEXT("HAZARD LIGHTS: ON (X)")
				: TEXT("HAZARD LIGHTS: OFF (X)"));
	}
}
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
