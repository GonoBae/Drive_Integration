#include "CrashVisualQaCommandlet.h"

#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCoreStructureDamageActor.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogCrashVisualQa, Log, All);

namespace
{
struct FFixtureWorld
{
	UWorld* World = nullptr;
	FFixtureWorld()
	{
		const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
			.RequiresHitProxies(false).CreatePhysicsScene(false).CreateNavigation(false)
			.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CrashVisualQa"),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
		if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	}
	~FFixtureWorld()
	{
		if (World)
		{
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}
	}
};

AStaticMeshActor* Box(UWorld* World, const FVector& Location, const FVector& Scale, const FLinearColor& Color)
{
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
	if (!Actor) return nullptr;
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* Material = Base ? UMaterialInstanceDynamic::Create(Base, Actor) : nullptr;
	if (Material) { Material->SetVectorParameterValue(TEXT("Color"), Color); Mesh->SetMaterial(0, Material); }
	Actor->SetActorTransform(FTransform(FRotator::ZeroRotator, Location, Scale));
	return Actor;
}

void Light(UWorld* World, const FRotator& Rotation, float Intensity, const FLinearColor& Color)
{
	ADirectionalLight* Actor = World->SpawnActor<ADirectionalLight>();
	if (!Actor) return;
	Actor->GetComponent()->SetMobility(EComponentMobility::Movable);
	Actor->GetComponent()->SetIntensity(Intensity);
	Actor->GetComponent()->SetLightColor(Color);
	Actor->GetComponent()->SetCastShadows(false);
	Actor->SetActorRotation(Rotation);
}

bool Capture(UWorld* World, USceneCaptureComponent2D* Camera, UTextureRenderTarget2D* Target,
	const FVector& Position, const FVector& LookAt, const FString& Path)
{
	Camera->SetWorldLocationAndRotation(Position, (LookAt - Position).Rotation());
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	World->UpdateWorldComponents(true, false);
	World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		Camera->CaptureScene();
		FlushRenderingCommands();
	}
	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Target->SizeX * Target->SizeY)
	{
		UE_LOG(LogCrashVisualQa, Error, TEXT("GPU readback failed: %s"), *Path);
		return false;
	}
	uint8 MinValue = 255, MaxValue = 0;
	for (const FColor& Pixel : Pixels)
	{
		const uint8 Value = FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
		MinValue = FMath::Min(MinValue, Value); MaxValue = FMath::Max(MaxValue, Value);
	}
	if (MaxValue - MinValue < 12)
	{
		UE_LOG(LogCrashVisualQa, Error, TEXT("Capture is blank/uniform, not acceptable GPU evidence: %s"), *Path);
		return false;
	}
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path));
	if (!File || !FImageUtils::ExportRenderTarget2DAsPNG(Target, *File)) return false;
	File->Close();
	UE_LOG(LogCrashVisualQa, Display, TEXT("Authored presentation screenshot: %s"), *Path);
	return IFileManager::Get().FileSize(*Path) > 0;
}
}

UCrashVisualQaCommandlet::UCrashVisualQaCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UCrashVisualQaCommandlet::Main(const FString& Params)
{
	if (!FApp::CanEverRender() || !GDynamicRHI
		|| FString(GDynamicRHI->GetName()).Contains(TEXT("Null"))
		|| FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
	{
		UE_LOG(LogCrashVisualQa, Error,
			TEXT("A real GPU is required. Use -AllowCommandletRendering -RenderOffscreen, never -NullRHI."));
		return 1;
	}
	FString Output;
	if (!FParse::Value(*Params, TEXT("OutputDir="), Output) || Output.IsEmpty())
	{
		UE_LOG(LogCrashVisualQa, Error, TEXT("Required: -OutputDir=<new screenshot directory>"));
		return 1;
	}
	Output = FPaths::ConvertRelativePathToFull(Output);
	if (!IFileManager::Get().MakeDirectory(*Output, true)) return 1;
	for (const TCHAR* Name : {TEXT("npc_intact.png"), TEXT("npc_front_damage.png"), TEXT("wall_full_vs_corner_debris.png")})
	{
		if (IFileManager::Get().FileExists(*(Output / Name)))
		{
			UE_LOG(LogCrashVisualQa, Error, TEXT("Refusing to overwrite prior QA screenshots. Use a new OutputDir."));
			return 1;
		}
	}
	FFixtureWorld Fixture;
	UWorld* World = Fixture.World;
	if (!World || !World->Scene) return 1;
	Box(World, FVector(0, 0, -10), FVector(40, 40, 0.2), FLinearColor(0.14f, 0.17f, 0.20f));
	Light(World, FRotator(-40, -35, 0), 4.0f, FLinearColor(1.0f, 0.92f, 0.82f));
	Light(World, FRotator(-30, 145, 0), 2.0f, FLinearColor(0.75f, 0.86f, 1.0f));
	AActor* CameraActor = World->SpawnActor<AActor>();
	USceneCaptureComponent2D* Camera = NewObject<USceneCaptureComponent2D>(CameraActor);
	CameraActor->SetRootComponent(Camera);
	Camera->RegisterComponent();
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(CameraActor);
	Target->ClearColor = FLinearColor(0.025f, 0.035f, 0.055f);
	Target->InitCustomFormat(1280, 720, PF_B8G8R8A8, false);
	Target->UpdateResourceImmediate(true);
	Camera->TextureTarget = Target;
	Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Camera->bCaptureEveryFrame = false;
	Camera->bCaptureOnMovement = false;
	Camera->FOVAngle = 50.0f;
	Camera->ShowFlags.SetMotionBlur(false);
	Camera->ShowFlags.SetTemporalAA(false);
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.0f;
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.0f;
	Camera->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	Camera->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	Camera->PostProcessSettings.bOverride_ReflectionMethod = true;
	Camera->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	ASimCoreNpcPresentationActor* Npc = World->SpawnActor<ASimCoreNpcPresentationActor>();
	if (!Npc || !Npc->HasAuthoredSedan()) return 1;
	SimCoreProtocol::FVehicleState State;
	State.EntityId = 1001;
	State.EntityKind = SimCoreProtocol::EEntityKind::NpcVehicle;
	State.PositionEnu = FVector3d(0, 0, 0.85);
	State.CollisionHalfLengthMeters = 2.2f;
	State.CollisionHalfWidthMeters = 1.0f;
	State.CollisionHalfHeightMeters = 0.75f;
	if (!Npc->ApplySnapshot(State, 0, 0, false, 0, FVector::ZeroVector)) return 1;
	const FVector CarCamera(690, -550, 350), CarTarget(10, 0, 70);
	if (!Capture(World, Camera, Target, CarCamera, CarTarget, Output / TEXT("npc_intact.png"))) return 1;
	State.DamagePercent = 90;
	State.LastImpactImpulseNs = 18000;
	State.CollisionEventSequence = 1;
	State.DamageZone = SimCoreProtocol::EVehicleDamageZone::Front;
	State.DentPatches.Add({FVector2D(1,0),FVector2D(-1,0),.7f,.25f});
	if (!Npc->ApplySnapshot(State, 0, 0, false, 0, FVector::ZeroVector)) return 1;
	if (!Capture(World, Camera, Target, CarCamera, CarTarget, Output / TEXT("npc_front_damage.png"))) return 1;
	Npc->SetActorHiddenInGame(true);
	Box(World, FVector(0, 0, 150), FVector(0.5, 12.0, 3.0), FLinearColor(0.61f, 0.58f, 0.51f));
	ASimCoreStructureDamageActor* Damage = World->SpawnActor<ASimCoreStructureDamageActor>();
	if (!Damage) return 1;
	SimCoreProtocol::FStructureState Wall;
	Wall.ColliderId = TEXT("qa_facade");
	Wall.Kind = SimCoreProtocol::EStructureKind::Building;
	Wall.DamagePercent = 55;
	Wall.EventSequence = 1;
	Wall.ImpactPointEnu = FVector3d(2.5, 0.25, 1.1);
	Wall.ImpactNormalEnu = FVector3d(0, 1, 0);
	Wall.ImpactHalfWidthMeters = 1.0f;
	Wall.ImpactHalfHeightMeters = 0.65f;
	Wall.ImpactSeverity = 0.65f;
	Damage->ApplyAuthoritativeDamage(Wall);
	Wall.EventSequence = 2;
	Wall.ImpactPointEnu.X = -2.5;
	Wall.ImpactHalfWidthMeters = 0.12f;
	Damage->ApplyAuthoritativeDamage(Wall);
	Damage->Tick(0.1f);
	Damage->Tick(0.1f);
	if (!Capture(World, Camera, Target, FVector(1450, -100, 620), FVector(0, 0, 135),
		Output / TEXT("wall_full_vs_corner_debris.png"))) return 1;
	FFileHelper::SaveStringToFile(TEXT("Authored-state GPU presentation fixture only.\n")
		TEXT("No server collision solve, PIE input, network or real driving was exercised.\n")
		TEXT("NPC views use the same camera before/after front damage. Wall: left full bumper, right narrow corner; debris age 0.20 seconds.\n"),
		*(Output / TEXT("README.txt")));
	UE_LOG(LogCrashVisualQa, Display, TEXT("PASS GPU fixture: 3 screenshots; RHI=%s; output=%s"), GDynamicRHI->GetName(), *Output);
	return 0;
}
