#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "ExternalVehiclePawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "SimCoreSedanVisualContract.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogVehicleVisualQa, Log, All);

namespace
{
// Opt-in editor-build integration QA. It injects only camera keys through the
// real player input path, never drive commands or authoritative vehicle poses.
class FVehicleVisualQa
{
public:
	~FVehicleVisualQa()
	{
		if (Handle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Handle); }
	}

	void Start()
	{
		if (Handle.IsValid()) { UE_LOG(LogVehicleVisualQa, Error, TEXT("QA already running.")); return; }
		UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		Controller = World ? World->GetFirstPlayerController() : nullptr;
		Pawn = Controller.IsValid() ? Cast<AExternalVehiclePawn>(Controller->GetPawn()) : nullptr;
		Boom = Pawn.IsValid() ? Pawn->FindComponentByClass<USpringArmComponent>() : nullptr;
		Camera = Pawn.IsValid() ? Pawn->FindComponentByClass<UCameraComponent>() : nullptr;
		if (!Pawn.IsValid() || !Boom.IsValid() || !Camera.IsValid())
		{
			UE_LOG(LogVehicleVisualQa, Error, TEXT("A rendering ExternalVehiclePawn game viewport is required."));
			return;
		}
		Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"))
			/ FString::Printf(TEXT("VehicleVisual-%s-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
		IFileManager::Get().MakeDirectory(*Directory, true);
		Stage = 0;
		StageTime = 0.0;
		StartTime = FPlatformTime::Seconds();
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FVehicleVisualQa::Tick));
		UE_LOG(LogVehicleVisualQa, Display, TEXT("Camera input/visual QA started: %s"), *Directory);
	}

private:
	bool Finish(bool bSuccess, const FString& Reason)
	{
		if (Wall.IsValid()) { Wall->Destroy(); Wall.Reset(); }
		if (Controller.IsValid()) { Key(EKeys::C, IE_Released, 0.0f); }
		if (bSuccess) { UE_LOG(LogVehicleVisualQa, Display, TEXT("PASS: %s; screenshots=%s"), *Reason, *Directory); }
		else { UE_LOG(LogVehicleVisualQa, Error, TEXT("FAIL stage %d: %s"), Stage, *Reason); }
		Handle.Reset();
		return false;
	}

	void Key(const FKey& Code, EInputEvent Event, float Amount)
	{
		if (Controller.IsValid())
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Code, Event, Amount, 1));
		}
	}

	void Advance() { ++Stage; StageTime = 0.0; }
	void Shot(const TCHAR* Name)
	{
		PendingShot = Directory / Name;
		FScreenshotRequest::RequestScreenshot(PendingShot, false, false);
	}
	bool ShotSaved() const { return IFileManager::Get().FileSize(*PendingShot) > 0; }
	FVector PivotLocation() const { return Boom->GetComponentLocation() + Boom->TargetOffset; }
	float YawOffset() const
	{
		return FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw, Boom->GetComponentRotation().Yaw);
	}

	bool Tick(float DeltaSeconds)
	{
		if (!Pawn.IsValid() || !Boom.IsValid() || !Camera.IsValid()) { return Finish(false, TEXT("view closed")); }
		if (FPlatformTime::Seconds() - StartTime > 90.0) { return Finish(false, TEXT("bounded QA timeout")); }
		StageTime += FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, 0.25);
		if (Stage == 0)
		{
			if (StageTime < 10.0 || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())) { return true; }
			int32 NewBodyCount = 0;
			int32 NewWheelCount = 0;
			TArray<UStaticMeshComponent*> Meshes;
			Pawn->GetComponents(Meshes);
			for (UStaticMeshComponent* Mesh : Meshes)
			{
				const FString Path = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName() : FString();
				NewBodyCount += Path == SimCoreSedanVisualContract::BodyObjectPath() ? 1 : 0;
				NewWheelCount += Path == SimCoreSedanVisualContract::WheelObjectPath() ? 1 : 0;
				if (Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
				{
					return Finish(false, TEXT("presentation mesh unexpectedly participates in vehicle physics"));
				}
			}
			if (NewBodyCount != 1 || NewWheelCount != 4) { return Finish(false, TEXT("expected sedan body and four independent wheel assets")); }
			StartPosition = Pawn->GetActorLocation();
			StartYaw = Pawn->GetActorRotation().Yaw;
			Shot(TEXT("rear.png"));
			Advance();
		}
		else if (Stage == 1 && ShotSaved())
		{
			Key(EKeys::MouseX, IE_Axis, 600.0f);
			Key(EKeys::MouseY, IE_Axis, -25.0f);
			Key(EKeys::MouseWheelAxis, IE_Axis, 1.0f);
			Advance();
		}
		else if (Stage == 2 && StageTime > 0.35)
		{
			const FRotator View = Boom->GetComponentRotation();
			UE_LOG(LogVehicleVisualQa, Display, TEXT("Mouse input observed: yawOffset=%.2f pitch=%.2f arm=%.2f"),
				YawOffset(), View.Pitch, Boom->TargetArmLength);
			if (FMath::Abs(YawOffset()) < 30.0f || View.Pitch > -17.0f || Boom->TargetArmLength >= 590.0f)
			{
				return Finish(false, TEXT("real mouse/zoom bindings did not move the camera"));
			}
			Shot(TEXT("front-three-quarter.png"));
			Advance();
		}
		else if (Stage == 3 && ShotSaved())
		{
			Key(EKeys::MouseX, IE_Axis, 225.0f);
			Advance();
		}
		else if (Stage == 4 && StageTime > 0.35)
		{
			Shot(TEXT("front.png"));
			Advance();
		}
		else if (Stage == 5 && ShotSaved())
		{
			Key(EKeys::C, IE_Pressed, 1.0f);
			Advance();
		}
		else if (Stage == 6 && StageTime > 0.35)
		{
			Key(EKeys::C, IE_Released, 0.0f);
			if (FMath::Abs(YawOffset()) > 0.1f || FMath::Abs(Boom->GetComponentRotation().Pitch + 15.0f) > 0.1f
				|| FMath::Abs(Boom->TargetArmLength - 600.0f) > 0.1f)
			{
				return Finish(false, TEXT("C did not restore the rear chase view"));
			}
			const FVector Centre = PivotLocation() - Boom->GetForwardVector() * 300.0f;
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Wall = Pawn->GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
			if (!Wall.IsValid()) { return Finish(false, TEXT("camera collision fixture creation failed")); }
			UStaticMeshComponent* WallMesh = Wall->GetStaticMeshComponent();
			WallMesh->SetMobility(EComponentMobility::Movable);
			WallMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			WallMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			WallMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
			WallMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			Wall->SetActorTransform(FTransform(FRotator(0.0, Boom->GetComponentRotation().Yaw, 0.0), Centre, FVector(0.2, 4.0, 3.0)));
			Advance();
		}
		else if (Stage == 7 && StageTime > 0.35)
		{
			const double Distance = FVector::Distance(Camera->GetComponentLocation(), PivotLocation());
			if (!Boom->bDoCollisionTest || Distance > 450.0) { return Finish(false, TEXT("camera did not retract ahead of the camera-only wall")); }
			UE_LOG(LogVehicleVisualQa, Display, TEXT("Camera collision retract distance=%.2fcm target=%.2fcm"), Distance, Boom->TargetArmLength);
			Wall->Destroy(); Wall.Reset();
			Advance();
		}
		else if (Stage == 8 && StageTime > 0.35)
		{
			if (FVector::Distance(Camera->GetComponentLocation(), PivotLocation()) < 590.0)
			{
				return Finish(false, TEXT("camera did not recover after removing the temporary wall"));
			}
			if (FVector::Distance(StartPosition, Pawn->GetActorLocation()) > 3.0
				|| FMath::Abs(FMath::FindDeltaAngleDegrees(StartYaw, Pawn->GetActorRotation().Yaw)) > 0.1)
			{
				return Finish(false, TEXT("camera-only inputs unexpectedly moved the stationary vehicle"));
			}
			return Finish(true, TEXT("sedan assets, actual mouse orbit/zoom/C reset, camera collision/recovery, vehicle isolation"));
		}
		return true;
	}

	FTSTicker::FDelegateHandle Handle;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<AExternalVehiclePawn> Pawn;
	TWeakObjectPtr<USpringArmComponent> Boom;
	TWeakObjectPtr<UCameraComponent> Camera;
	TWeakObjectPtr<AStaticMeshActor> Wall;
	FString Directory;
	FString PendingShot;
	FVector StartPosition = FVector::ZeroVector;
	float StartYaw = 0.0f;
	double StartTime = 0.0;
	double StageTime = 0.0;
	int32 Stage = 0;
};

FVehicleVisualQa Service;
FAutoConsoleCommand Command(TEXT("VehicleVisual.QAViews"),
	TEXT("Editor-build QA: capture rear/front views using real camera key input, verify zoom/reset and camera collision."),
	FConsoleCommandDelegate::CreateLambda([]() { Service.Start(); }));
}
