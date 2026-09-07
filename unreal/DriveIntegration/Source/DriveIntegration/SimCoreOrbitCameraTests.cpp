#include "SimCoreOrbitCamera.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ExternalVehiclePawn.h"
#include "SimCoreClientComponent.h"
#include "SimCoreDriverPresentation.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreOrbitInputRatesTest,
	"DriveIntegration.Camera.FrameIndependentOrbit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreOrbitInputRatesTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreOrbitCamera;
	bool bSuccess = true;
	for (int32 FramesPerSecond : {30, 60, 120, 240})
	{
		FState Mouse;
		FState Gamepad;
		for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
		{
			// Same physical mouse displacement over one second at each FPS.
			AddMouseDelta(Mouse, 450.0f / FramesPerSecond, -100.0f / FramesPerSecond, 0.2f);
			AddGamepadRate(Gamepad, 0.5f, -0.25f, 90.0f, 1.0f / FramesPerSecond);
		}
		bSuccess &= TestTrue(TEXT("mouse yaw uses displacement without an extra delta time"),
			FMath::IsNearlyEqual(Mouse.YawOffsetDegrees, 90.0f, 0.001f));
		bSuccess &= TestTrue(TEXT("mouse pitch is independent of frame count"),
			FMath::IsNearlyEqual(Mouse.PitchDegrees, -35.0f, 0.001f));
		bSuccess &= TestTrue(TEXT("gamepad yaw is a rate integrated over elapsed seconds"),
			FMath::IsNearlyEqual(Gamepad.YawOffsetDegrees, 45.0f, 0.001f));
		bSuccess &= TestTrue(TEXT("gamepad pitch is independent of frame count"),
			FMath::IsNearlyEqual(Gamepad.PitchDegrees, -37.5f, 0.001f));
	}
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreOrbitLimitsTest,
	"DriveIntegration.Camera.LimitsResetAndHorizon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreOrbitLimitsTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreOrbitCamera;
	bool bSuccess = true;
	FState State;
	AddMouseDelta(State, 2150.0f, 1000.0f, 0.2f);
	bSuccess &= TestTrue(TEXT("unbounded yaw input wraps, without a hard orbit stop"),
		FMath::IsNearlyEqual(State.YawOffsetDegrees, 70.0f));
	bSuccess &= TestEqual(TEXT("pitch cannot flip over the target"), State.PitchDegrees, MaxPitchDegrees);
	AddMouseDelta(State, 0.0f, -1000.0f, 0.2f);
	bSuccess &= TestEqual(TEXT("pitch cannot orbit underneath the road"), State.PitchDegrees, MinPitchDegrees);
	AddZoom(State, 100.0f, 75.0f);
	bSuccess &= TestEqual(TEXT("positive wheel zooms in to a safe requested distance"), State.DistanceCm, MinDistanceCm);
	AddZoom(State, -100.0f, 75.0f);
	bSuccess &= TestEqual(TEXT("negative wheel zooms out to the configured limit"), State.DistanceCm, MaxDistanceCm);
	const FRotator View = BuildWorldRotation(State, 150.0f);
	bSuccess &= TestTrue(TEXT("view follows vehicle yaw plus the persistent orbit"), FMath::IsNearlyEqual(View.Yaw, -140.0));
	bSuccess &= TestEqual(TEXT("horizon always has zero roll"), View.Roll, 0.0);
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		AddMouseDelta(State, 0.0f, 0.0f, 0.2f);
		AddGamepadRate(State, 0.0f, 0.0f, 90.0f, 1.0f / 60.0f);
	}
	bSuccess &= TestTrue(TEXT("free orbit does not auto-snap behind the vehicle"),
		FMath::IsNearlyEqual(State.YawOffsetDegrees, 70.0f));
	bSuccess &= TestEqual(TEXT("idle frames do not reset zoom"), State.DistanceCm, MaxDistanceCm);
	ResetView(State);
	bSuccess &= TestEqual(TEXT("reset restores rear view"), State.YawOffsetDegrees, 0.0f);
	bSuccess &= TestEqual(TEXT("reset restores useful chase pitch"), State.PitchDegrees, DefaultPitchDegrees);
	bSuccess &= TestEqual(TEXT("reset restores the 6m chase distance"), State.DistanceCm, DefaultDistanceCm);
	const float NotFinite = std::numeric_limits<float>::quiet_NaN();
	AddMouseDelta(State, NotFinite, NotFinite, 0.2f);
	AddGamepadRate(State, NotFinite, 1.0f, 90.0f, NotFinite);
	AddZoom(State, NotFinite, 75.0f);
	bSuccess &= TestFalse(TEXT("invalid presentation input cannot create a NaN view"),
		BuildWorldRotation(State, NotFinite).ContainsNaN());
	bSuccess &= TestEqual(TEXT("invalid zoom input leaves a finite distance"), State.DistanceCm, DefaultDistanceCm);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreOrbitInputBindingsTest,
	"DriveIntegration.Camera.InputBindingsAndOfflinePawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreOrbitInputBindingsTest::RunTest(const FString& Parameters)
{
	bool bSuccess = true;
	const UInputSettings* Settings = UInputSettings::GetInputSettings();
	if (!TestNotNull(TEXT("project input settings"), Settings))
	{
		return false;
	}
	const auto HasAxis = [Settings](FName Name, FKey Key)
	{
		TArray<FInputAxisKeyMapping> Mappings;
		Settings->GetAxisMappingByName(Name, Mappings);
		return Mappings.ContainsByPredicate([Key](const FInputAxisKeyMapping& Mapping)
		{
			return Mapping.Key == Key && FMath::IsNearlyEqual(Mapping.Scale, 1.0f);
		});
	};
	bSuccess &= TestTrue(TEXT("mouse X mapped to orbit yaw"), HasAxis(TEXT("CameraOrbitYaw"), EKeys::MouseX));
	bSuccess &= TestTrue(TEXT("mouse Y mapped to orbit pitch"), HasAxis(TEXT("CameraOrbitPitch"), EKeys::MouseY));
	bSuccess &= TestTrue(TEXT("wheel mapped to zoom"), HasAxis(TEXT("CameraZoom"), EKeys::MouseWheelAxis));
	bSuccess &= TestTrue(TEXT("right stick X mapped to rate yaw"), HasAxis(TEXT("CameraGamepadYaw"), EKeys::Gamepad_RightX));
	bSuccess &= TestTrue(TEXT("right stick Y mapped to rate pitch"), HasAxis(TEXT("CameraGamepadPitch"), EKeys::Gamepad_RightY));
	TArray<FInputActionKeyMapping> ResetMappings;
	Settings->GetActionMappingByName(TEXT("CameraReset"), ResetMappings);
	bSuccess &= TestTrue(TEXT("C resets camera"), ResetMappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::C; }));
	bSuccess &= TestTrue(TEXT("right stick click resets camera"), ResetMappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::Gamepad_RightThumbstick; }));
	TArray<FInputActionKeyMapping> CycleMappings;
	Settings->GetActionMappingByName(TEXT("CameraCycle"), CycleMappings);
	bSuccess &= TestTrue(TEXT("V cycles camera modes"), CycleMappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::V; }));
	bSuccess &= TestTrue(TEXT("left stick click cycles camera modes"), CycleMappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::Gamepad_LeftThumbstick; }));
	bSuccess &= TestFalse(TEXT("mouse smoothing does not introduce presentation lag"), Settings->bEnableMouseSmoothing);
	bSuccess &= TestFalse(TEXT("orbit sensitivity is not unexpectedly scaled by FOV"), Settings->bEnableFOVScaling);

	// No BeginPlay or world tick: this isolated transient world cannot open a
	// WebSocket or consume a live server's control lease while testing bindings.
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreCameraQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated camera QA world"), World))
	{
		return false;
	}
	AExternalVehiclePawn* Pawn = World->SpawnActor<AExternalVehiclePawn>();
	if (!TestNotNull(TEXT("camera QA vehicle"), Pawn))
	{
		World->DestroyWorld(false);
		return false;
	}
	UInputComponent* Input = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(Input);
	USpringArmComponent* Boom = Pawn->FindComponentByClass<USpringArmComponent>();
	USimCoreClientComponent* Client = Pawn->FindComponentByClass<USimCoreClientComponent>();
	if (!TestNotNull(TEXT("vehicle orbit boom"), Boom) || !TestNotNull(TEXT("vehicle client component"), Client))
	{
		World->DestroyWorld(false);
		return false;
	}
	bSuccess &= TestTrue(TEXT("camera probe blocks scenery clipping"), Boom->bDoCollisionTest && Boom->ProbeChannel == ECC_Camera);
	bSuccess &= TestTrue(TEXT("absolute camera rotation rejects body pitch and roll"), Boom->IsUsingAbsoluteRotation());
	bSuccess &= TestFalse(TEXT("position is not additionally smoothed"), Boom->bEnableCameraLag);
	bSuccess &= TestFalse(TEXT("rotation is not additionally smoothed"), Boom->bEnableCameraRotationLag);
	const auto ExecuteAxis = [Input](FName Name, float Value)
	{
		for (FInputAxisBinding& Binding : Input->AxisBindings)
		{
			if (Binding.AxisName == Name)
			{
				Binding.AxisDelegate.Execute(Value);
				return true;
			}
		}
		return false;
	};
	Pawn->SetActorLocationAndRotation(FVector(100.0, 200.0, 300.0), FRotator(20.0, 30.0, 12.0));
	const FTransform AuthoritativeTransform = Pawn->GetActorTransform();
	bSuccess &= TestTrue(TEXT("yaw binding executes"), ExecuteAxis(TEXT("CameraOrbitYaw"), 450.0f));
	bSuccess &= TestTrue(TEXT("pitch binding executes"), ExecuteAxis(TEXT("CameraOrbitPitch"), -500.0f));
	bSuccess &= TestTrue(TEXT("zoom binding executes"), ExecuteAxis(TEXT("CameraZoom"), 2.0f));
	Pawn->Tick(1.0f / 60.0f);
	bSuccess &= TestTrue(TEXT("bound mouse rotates view without any server state"),
		Boom->GetTargetRotation().Equals(FRotator(-65.0, 120.0, 0.0), 0.001));
	bSuccess &= TestEqual(TEXT("bound wheel adjusts requested boom distance"), Boom->TargetArmLength, 450.0f);
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		Pawn->Tick(1.0f / 60.0f);
	}
	bSuccess &= TestTrue(TEXT("idle vehicle keeps the requested free orbit"),
		Boom->GetTargetRotation().Equals(FRotator(-65.0, 120.0, 0.0), 0.001));
	bSuccess &= TestTrue(TEXT("camera controls never rotate or reposition the vehicle"),
		Pawn->GetActorTransform().Equals(AuthoritativeTransform, 0.001));
	bSuccess &= TestFalse(TEXT("camera controls do not initiate a network connection"), Client->IsConnected());
	bSuccess &= TestEqual(TEXT("camera-only input leaves the client disconnected"),
		Client->GetConnectionState(), ESimCoreConnectionState::Disconnected);
	bSuccess &= TestTrue(TEXT("gamepad yaw binding executes"), ExecuteAxis(TEXT("CameraGamepadYaw"), 1.0f));
	bSuccess &= TestTrue(TEXT("gamepad pitch binding executes"), ExecuteAxis(TEXT("CameraGamepadPitch"), 0.25f));
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Pawn->Tick(1.0f / 60.0f);
	}
	bSuccess &= TestTrue(TEXT("bound right stick adds 90deg yaw and 22.5deg pitch in one second"),
		Boom->GetTargetRotation().Equals(FRotator(-42.5, -150.0, 0.0), 0.001));
	ExecuteAxis(TEXT("CameraGamepadYaw"), 0.0f);
	ExecuteAxis(TEXT("CameraGamepadPitch"), 0.0f);
	bool bResetExecuted = false;
	for (int32 Index = 0; Index < Input->GetNumActionBindings(); ++Index)
	{
		FInputActionBinding& Binding = Input->GetActionBinding(Index);
		if (Binding.GetActionName() == TEXT("CameraReset") && Binding.KeyEvent == IE_Pressed)
		{
			Binding.ActionDelegate.Execute(EKeys::C);
			bResetExecuted = true;
			break;
		}
	}
	Pawn->Tick(1.0f / 60.0f);
	bSuccess &= TestTrue(TEXT("reset binding executes"), bResetExecuted);
	bSuccess &= TestTrue(TEXT("C restores rear view relative to current vehicle heading"),
		Boom->GetTargetRotation().Equals(FRotator(-15.0, 30.0, 0.0), 0.001));
	bSuccess &= TestEqual(TEXT("C also restores normal chase distance"), Boom->TargetArmLength, 600.0f);
	World->DestroyWorld(false);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreCameraModeMathTest,
	"DriveIntegration.Camera.DriverLimitsMountsAndModeCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreCameraModeMathTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreOrbitCamera;
	using SimCoreProtocol::ERuntimeVehicleClass;
	bool bSuccess = true;
	bSuccess &= TestTrue(TEXT("follow cycles to driver"), NextMode(EMode::Follow) == EMode::Driver);
	bSuccess &= TestTrue(TEXT("driver cycles to fixed rear"), NextMode(EMode::Driver) == EMode::FixedRear);
	bSuccess &= TestTrue(TEXT("fixed rear cycles to follow"), NextMode(EMode::FixedRear) == EMode::Follow);
	for (int32 FramesPerSecond : {30, 60, 120, 240})
	{
		FDriverLook Mouse;
		FDriverLook Gamepad;
		for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
		{
			AddDriverMouseDelta(Mouse, 225.0f / FramesPerSecond, -100.0f / FramesPerSecond, 0.2f);
			AddDriverGamepadRate(Gamepad, 0.5f, -0.25f, 90.0f, 1.0f / FramesPerSecond);
		}
		bSuccess &= TestTrue(TEXT("driver mouse delta remains frame independent"),
			FMath::IsNearlyEqual(Mouse.YawDegrees, 45.0f, 0.001f)
				&& FMath::IsNearlyEqual(Mouse.PitchDegrees, -20.0f, 0.001f));
		bSuccess &= TestTrue(TEXT("driver right stick remains frame independent"),
			FMath::IsNearlyEqual(Gamepad.YawDegrees, 45.0f, 0.001f)
				&& FMath::IsNearlyEqual(Gamepad.PitchDegrees, -22.5f, 0.001f));
	}
	FDriverLook Look;
	AddDriverMouseDelta(Look, 10000.0f, -10000.0f, 0.2f);
	bSuccess &= TestEqual(TEXT("driver cannot turn through the rear of the headrest"), Look.YawDegrees, 75.0f);
	bSuccess &= TestEqual(TEXT("driver cannot look through the floor"), Look.PitchDegrees, -40.0f);
	AddDriverMouseDelta(Look, -10000.0f, 10000.0f, 0.2f);
	bSuccess &= TestEqual(TEXT("left look is bounded"), Look.YawDegrees, -75.0f);
	bSuccess &= TestEqual(TEXT("up look is bounded"), Look.PitchDegrees, 30.0f);
	const FQuat BodyRotation = FRotator(19.0, 72.0, -12.0).Quaternion();
	Look = {};
	bSuccess &= TestTrue(TEXT("driver follows actual chassis pitch, yaw and roll"),
		BuildBodyRelativeRotation(Look, BodyRotation).Quaternion().Equals(BodyRotation, 0.0001));
	Look.YawDegrees = std::numeric_limits<float>::quiet_NaN();
	bSuccess &= TestFalse(TEXT("invalid look cannot poison camera rotation"),
		BuildBodyRelativeRotation(Look, BodyRotation).ContainsNaN());
	const FVehicleMounts Sedan = GetVehicleMounts(ERuntimeVehicleClass::Sedan);
	const FVehicleMounts Compact = GetVehicleMounts(ERuntimeVehicleClass::Compact);
	const FVehicleMounts Truck = GetVehicleMounts(ERuntimeVehicleClass::Truck);
	const FVehicleMounts Motorcycle = GetVehicleMounts(ERuntimeVehicleClass::Motorcycle);
	bSuccess &= TestTrue(TEXT("sedan view is at the driver side inside the greenhouse"),
		Sedan.DriverLocationCm.Y < 0.0 && Sedan.DriverLocationCm.Z > 54.0 && Sedan.DriverLocationCm.Z < 95.0);
	bSuccess &= TestTrue(TEXT("compact mount follows its actual mesh scale and offset"),
		Compact.DriverLocationCm.Equals(Sedan.DriverLocationCm * FVector(0.79, 0.90, 0.92)
			+ FVector(-4.0, 0.0, -1.5)));
	bSuccess &= TestTrue(TEXT("truck view is inside the hollow cab at its driver seat"),
		!Truck.bDriverUsesCabFrontFallback && Truck.DriverLocationCm.Equals(FVector(95.0, -34.0, 145.0)));
	bSuccess &= TestTrue(TEXT("truck rear view clears the tall cargo body"),
		Truck.FixedTargetHeightCm > 164.0 && Truck.FixedDistanceCm > Sedan.FixedDistanceCm);
	bSuccess &= TestTrue(TEXT("motorcycle rider view sits above the handlebar and on centerline"),
		Motorcycle.DriverLocationCm.Z > 70.0 && Motorcycle.DriverLocationCm.Y == 0.0);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreCameraModesPawnTest,
	"DriveIntegration.Camera.ModeSwitchClassMountsAndOwnerVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreCameraModesPawnTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreOrbitCamera;
	using SimCoreProtocol::ERuntimeVehicleClass;
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreCameraModesQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated camera modes QA world"), World)) return false;
	AExternalVehiclePawn* Pawn = World->SpawnActor<AExternalVehiclePawn>();
	if (!TestNotNull(TEXT("camera modes QA pawn"), Pawn))
	{
		World->DestroyWorld(false);
		return false;
	}
	bool bSuccess = true;
	UInputComponent* Input = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(Input);
	bool bCycleExecuted = false;
	for (int32 Index = 0; Index < Input->GetNumActionBindings(); ++Index)
	{
		FInputActionBinding& Binding = Input->GetActionBinding(Index);
		if (Binding.GetActionName() == TEXT("CameraCycle") && Binding.KeyEvent == IE_Pressed)
		{
			Binding.ActionDelegate.Execute(EKeys::V);
			bCycleExecuted = true;
			break;
		}
	}
	bSuccess &= TestTrue(TEXT("V action enters driver mode without BeginPlay or server"),
		bCycleExecuted && Pawn->GetCameraMode() == EMode::Driver);
	Pawn->SetActorLocationAndRotation(FVector(100, 200, 300), FRotator(18, 37, -11));
	const FTransform AuthoritativeTransform = Pawn->GetActorTransform();
	for (const ERuntimeVehicleClass VehicleClass : {ERuntimeVehicleClass::Sedan,
		ERuntimeVehicleClass::Compact, ERuntimeVehicleClass::Truck, ERuntimeVehicleClass::Motorcycle})
	{
		bSuccess &= TestTrue(TEXT("camera QA class assets configure"), Pawn->ConfigureVehicleClass(VehicleClass));
		Pawn->UpdateOrbitCamera(0.0f);
		const FVehicleMounts Mounts = GetVehicleMounts(VehicleClass);
		bSuccess &= TestTrue(TEXT("driver mount updates with accepted vehicle class"),
			Pawn->CameraBoom->TargetOffset.Equals(Pawn->GetActorQuat().RotateVector(Mounts.DriverLocationCm), 0.001));
		bSuccess &= TestEqual(TEXT("driver mount has no chase arm"), Pawn->CameraBoom->TargetArmLength, 0.0f);
		bSuccess &= TestFalse(TEXT("driver mount cannot be pulled through its own cabin"), Pawn->CameraBoom->bDoCollisionTest);
		bSuccess &= TestTrue(TEXT("driver follows chassis rotation instead of a world-level horizon"),
			Pawn->CameraBoom->GetTargetRotation().Quaternion().Equals(Pawn->GetActorQuat(), 0.001));
	}
	Pawn->ConfigureVehicleClass(ERuntimeVehicleClass::Sedan);
	Pawn->UpdateOrbitCamera(0.0f);
	UPoseableMeshComponent* DriverMesh = Pawn->DriverPresentation->GetDriverMesh();
	if (TestNotNull(TEXT("sedan has seated driver mesh"), DriverMesh))
	{
		bSuccess &= TestTrue(TEXT("seated body cannot occlude its owner's driver camera"), DriverMesh->bOwnerNoSee);
	}
	else bSuccess = false;
	bSuccess &= TestTrue(TEXT("driver view keeps cabin enabled"), Pawn->DriverPresentation->IsVisible());
	Pawn->CycleCameraView();
	const FRotator FixedRotation = Pawn->CameraBoom->GetTargetRotation();
	const float FixedDistance = Pawn->CameraBoom->TargetArmLength;
	Pawn->OrbitCameraYaw(500.0f);
	Pawn->OrbitCameraPitch(500.0f);
	Pawn->ZoomCamera(10.0f);
	Pawn->SetCameraGamepadYaw(1.0f);
	Pawn->UpdateOrbitCamera(1.0f);
	bSuccess &= TestTrue(TEXT("fixed view ignores look and zoom inputs"),
		Pawn->CameraBoom->GetTargetRotation().Equals(FixedRotation, 0.001)
			&& Pawn->CameraBoom->TargetArmLength == FixedDistance);
	bSuccess &= TestTrue(TEXT("fixed external view restores obstacle probing"), Pawn->CameraBoom->bDoCollisionTest);
	if (DriverMesh) bSuccess &= TestFalse(TEXT("external view restores the driver"), DriverMesh->bOwnerNoSee);
	Pawn->SetCameraGamepadYaw(0.0f);
	Pawn->ResetCameraView();
	bSuccess &= TestTrue(TEXT("C restores follow mode from any mode"), Pawn->GetCameraMode() == EMode::Follow);
	bSuccess &= TestTrue(TEXT("C restores rear follow with a level horizon"),
		Pawn->CameraBoom->GetTargetRotation().Equals(FRotator(-15.0, 37.0, 0.0), 0.001));
	bSuccess &= TestEqual(TEXT("C restores normal orbit distance"), Pawn->CameraBoom->TargetArmLength, 600.0f);
	bSuccess &= TestTrue(TEXT("camera modes do not move the authoritative pawn"),
		Pawn->GetActorTransform().Equals(AuthoritativeTransform, 0.001));
	bSuccess &= TestFalse(TEXT("camera modes do not connect to a server"), Pawn->SimCoreClient->IsConnected());
	World->DestroyWorld(false);
	return bSuccess;
}

#endif
