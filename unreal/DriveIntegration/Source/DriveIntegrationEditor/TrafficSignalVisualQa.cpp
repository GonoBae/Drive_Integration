#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "SimCoreTrafficSignalActor.h"
#include "SimCoreVirtualCityTrafficLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrafficSignalVisualQa, Log, All);

namespace
{
using FTrafficQaAspect = SimCoreProtocol::ETrafficSignalAspect;

const TCHAR* TrafficQaAspectName(FTrafficQaAspect Aspect)
{
	switch (Aspect)
	{
	case FTrafficQaAspect::Red: return TEXT("RED");
	case FTrafficQaAspect::Yellow: return TEXT("YELLOW");
	case FTrafficQaAspect::Green: return TEXT("GREEN");
	default: return TEXT("UNKNOWN");
	}
}

/** Opt-in, editor-module integration probe. Never injects phases, drive commands,
 * actor poses, or materials. Only its transient camera and new PNGs are owned. */
class FTrafficSignalVisualQa
{
public:
	~FTrafficSignalVisualQa()
	{
		if (TickerHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle); }
		RestoreView();
		RemoveScreenshotObserver();
	}

	void Start()
	{
		if (TickerHandle.IsValid())
		{
			UE_LOG(LogTrafficSignalVisualQa, Error, TEXT("QA already running; existing probe was not replaced.")); return;
		}
		if (!FApp::CanEverRender())
		{
			UE_LOG(LogTrafficSignalVisualQa, Error, TEXT("FAIL: TrafficVisual.QAViews requires a rendering -game process.")); return;
		}
		Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"))
			/ FString::Printf(TEXT("TrafficSignals-%s-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			UE_LOG(LogTrafficSignalVisualQa, Error, TEXT("FAIL: cannot create new screenshot directory %s"), *Directory); return;
		}
		AuthoredSignals = SimCoreVirtualCity::BuildTrafficLayout().Signals;
		StartWallTime = FPlatformTime::Seconds();
		NormalTickTime = 0;
		NormalTickCount = 0;
		QuietShaderTicks = 0;
		CapturedMask = 0;
		StableAspectTicks = 0;
		LastAspect = FTrafficQaAspect::Unknown;
		RequestedAspect = FTrafficQaAspect::Unknown;
		bPendingShot = false;
		bShotProcessed = false;
		PendingShot.Reset();
		DeferredFailure.Reset();
		WaitingReason = TEXT("waiting for the -game world, player controller, and exactly three server-created heads");
		ScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(this, &FTrafficSignalVisualQa::OnScreenshotProcessed);
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTrafficSignalVisualQa::Tick));
		UE_LOG(LogTrafficSignalVisualQa, Display,
			TEXT("TrafficVisual.QAViews started: real server phases only, signal_id=1 camera, >=10s normal ticks, shader-idle gate, 45s timeout; output=%s"), *Directory);
	}

private:
	void RemoveScreenshotObserver()
	{
		if (ScreenshotHandle.IsValid())
		{
			FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
			ScreenshotHandle.Reset();
		}
	}

	void RestoreView()
	{
		if (Controller.IsValid() && ProbeCamera.IsValid() && Controller->GetViewTarget() == ProbeCamera.Get())
		{
			// Do not overwrite a later view-target change made by the user/another tool.
			AActor* Target = OriginalViewTarget.Get();
			if (!Target) { Target = Controller->GetPawn(); }
			if (Target) { Controller->SetViewTarget(Target); }
		}
		if (ProbeCamera.IsValid()) { ProbeCamera->Destroy(); ProbeCamera.Reset(); }
		OriginalViewTarget.Reset();
		Controller.Reset();
		GameWorld.Reset();
	}

	bool Finish(bool bSuccess, const FString& Reason)
	{
		RemoveScreenshotObserver();
		RestoreView();
		if (bSuccess)
		{
			UE_LOG(LogTrafficSignalVisualQa, Display,
				TEXT("PASS: %s; three-head authoritative one-hot/NoCollision/lens/material checks; RED,YELLOW,GREEN PNGs=%s; original view restored, only QA camera destroyed."),
				*Reason, *Directory);
		}
		else
		{
			UE_LOG(LogTrafficSignalVisualQa, Error,
				TEXT("FAIL: %s; capturedMask=%u normalTicks=%d normalTickTime=%.2f; output=%s; QA camera removed, saved maps/materials/vehicle poses untouched."),
				*Reason, CapturedMask, NormalTickCount, NormalTickTime, *Directory);
		}
		TickerHandle.Reset();
		return false;
	}

	bool GetThreeHeads(UWorld* World, TArray<ASimCoreTrafficSignalActor*>& Heads, FString& Reason) const
	{
		Heads.Reset();
		for (TActorIterator<ASimCoreTrafficSignalActor> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed()) { Heads.Add(*It); }
		}
		if (Heads.Num() != 3)
		{
			Reason = FString::Printf(TEXT("expected exactly three server-created heads, observed %d"), Heads.Num()); return false;
		}
		Heads.Sort([](const ASimCoreTrafficSignalActor& A, const ASimCoreTrafficSignalActor& B)
		{
			return A.GetSignalId() < B.GetSignalId();
		});
		for (int32 Index = 0; Index < Heads.Num(); ++Index)
		{
			if (Heads[Index]->GetSignalId() != static_cast<uint32>(Index + 1))
			{
				Reason = TEXT("three-head probe requires unique server signal IDs 1,2,3"); return false;
			}
		}
		return true;
	}

	bool ValidateSnapshot(const TArray<ASimCoreTrafficSignalActor*>& Heads, FString& Reason) const
	{
		const FLinearColor ActiveColors[] = { FLinearColor(1.0f,0.01f,0.005f),
			FLinearColor(1.0f,0.64f,0.005f), FLinearColor(0.005f,1.0f,0.025f) };
		const FLinearColor Dark(0.012f,0.012f,0.012f);
		for (const ASimCoreTrafficSignalActor* Head : Heads)
		{
			const auto& Display = Head->GetDisplayState();
			const bool Active[] = {Display.bRed, Display.bYellow, Display.bGreen};
			const int32 ActiveCount = int32(Active[0]) + int32(Active[1]) + int32(Active[2]);
			const int32 AspectIndex = static_cast<int32>(Display.Aspect) - 1;
			if (!Display.bVerified || Head->IsHidden() || ActiveCount != 1
				|| AspectIndex < 0 || AspectIndex > 2 || !Active[AspectIndex])
			{
				Reason = FString::Printf(TEXT("S%u is not a verified visible one-hot server aspect: %s"), Head->GetSignalId(), *Head->GetStatusText()); return false;
			}
			if (Head->GetActorEnableCollision())
			{
				Reason = FString::Printf(TEXT("S%u actor has collision enabled"), Head->GetSignalId()); return false;
			}
			TArray<UPrimitiveComponent*> Primitives;
			Head->GetComponents(Primitives);
			for (const UPrimitiveComponent* Primitive : Primitives)
			{
				if (Primitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
				{
					Reason = FString::Printf(TEXT("S%u component %s is not NoCollision"), Head->GetSignalId(), *Primitive->GetName()); return false;
				}
			}
			const auto* Authored = AuthoredSignals.FindByPredicate([Head](const auto& Signal) { return Signal.Id == Head->GetSignalId(); });
			if (!Authored || FMath::Abs(FMath::FindDeltaAngleDegrees(Head->GetActorRotation().Yaw, Authored->HeadingDegrees)) > 0.1
				|| FVector::DotProduct(Head->GetLensFacingDirection().GetSafeNormal(), -Head->GetActorForwardVector()) < 0.999)
			{
				Reason = FString::Printf(TEXT("S%u lens/heading differs from its authored approaching direction"), Head->GetSignalId()); return false;
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				UStaticMeshComponent* Lamp = Head->GetLamp(Index);
				if (!Lamp || !Lamp->GetStaticMesh() || !Lamp->IsVisible()
					|| FVector::DotProduct(Lamp->GetComponentLocation() - Head->GetActorLocation(), Head->GetActorForwardVector()) >= -1.0
					|| Lamp->GetRelativeScale3D().X >= Lamp->GetRelativeScale3D().Y
					|| Lamp->GetRelativeScale3D().X >= Lamp->GetRelativeScale3D().Z)
				{
					Reason = FString::Printf(TEXT("S%u lamp %d is missing, hidden, or not on the back-facing lens plane"), Head->GetSignalId(), Index); return false;
				}
				UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Lamp->GetMaterial(0));
				UMaterialInterface* Parent = Material ? Material->Parent.Get() : nullptr;
				if (!Parent)
				{
					Reason = FString::Printf(TEXT("S%u lamp %d has no dynamic color material parent"), Head->GetSignalId(), Index); return false;
				}
				TArray<FMaterialParameterInfo> Infos;
				TArray<FGuid> ParameterIds;
				// Check the parent, not just an MID override: setting an undeclared
				// parameter can appear successful while the rendered lamp stays gray.
				Parent->GetAllVectorParameterInfo(Infos, ParameterIds);
				const bool bDeclaresColor = Infos.ContainsByPredicate([](const FMaterialParameterInfo& Info)
				{
					return Info.Name.ToString().Equals(TEXT("Color"), ESearchCase::CaseSensitive)
						&& Info.Association == EMaterialParameterAssociation::GlobalParameter && Info.Index == INDEX_NONE;
				});
				if (!bDeclaresColor)
				{
					FString Declared;
					for (const auto& Info : Infos) { if (!Declared.IsEmpty()) { Declared += TEXT(","); } Declared += Info.Name.ToString(); }
					Reason = FString::Printf(TEXT("S%u lamp %d parent %s does not declare exact global Color vector parameter (declared=[%s]); MID overrides alone do not prove rendering"),
						Head->GetSignalId(), Index, *Parent->GetPathName(), *Declared); return false;
				}
				FLinearColor Actual = FLinearColor::Transparent;
				const FLinearColor Expected = Active[Index] ? ActiveColors[Index] : Dark;
				if (!Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Actual) || !Actual.Equals(Expected, 1.e-4f))
				{
					Reason = FString::Printf(TEXT("S%u lamp %d Color mismatch: got %s expected %s"),
						Head->GetSignalId(), Index, *Actual.ToString(), *Expected.ToString()); return false;
				}
			}
		}
		if (!ProbeCamera.IsValid() || Heads.IsEmpty()) { Reason = TEXT("QA camera disappeared"); return false; }
		const FVector TowardsCamera = (ProbeCamera->GetActorLocation() - Heads[0]->GetLamp(1)->GetComponentLocation()).GetSafeNormal();
		if (FVector::DotProduct(TowardsCamera, Heads[0]->GetLensFacingDirection()) < 0.9)
		{
			Reason = TEXT("signal 1 camera is not on the approaching driver's lens-facing side"); return false;
		}
		return true;
	}

	bool PlaceCamera(UWorld* World, APlayerController* Player, ASimCoreTrafficSignalActor* Head)
	{
		UStaticMeshComponent* MiddleLamp = Head->GetLamp(1);
		if (!MiddleLamp) { return false; }
		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags |= RF_Transient;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Location = Head->GetActorLocation() - Head->GetActorForwardVector() * 1000.0 + FVector(0,0,200.0);
		const FRotator Rotation = (MiddleLamp->GetComponentLocation() - Location).Rotation();
		ProbeCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(Rotation, Location), Spawn);
		if (!ProbeCamera.IsValid()) { return false; }
		ProbeCamera->SetActorEnableCollision(false);
		ProbeCamera->GetCameraComponent()->SetFieldOfView(25.0f);
		Controller = Player;
		GameWorld = World;
		OriginalViewTarget = Player->GetViewTarget();
		Player->SetViewTarget(ProbeCamera.Get());
		UE_LOG(LogTrafficSignalVisualQa, Display,
			TEXT("Three server heads found; temporary signal-1 camera location=%s target=%s. Vehicle transform/control inputs untouched."),
			*Location.ToString(), *MiddleLamp->GetComponentLocation().ToString());
		return true;
	}

	void OnScreenshotProcessed()
	{
		if (!bPendingShot || bShotProcessed) { return; }
		bShotProcessed = true;
		TArray<ASimCoreTrafficSignalActor*> Heads;
		if (!GameWorld.IsValid() || !GetThreeHeads(GameWorld.Get(), Heads, DeferredFailure))
		{
			if (DeferredFailure.IsEmpty()) { DeferredFailure = TEXT("game world closed during screenshot"); } return;
		}
		if (!ValidateSnapshot(Heads, DeferredFailure)) { return; }
		if (Heads[0]->GetDisplayState().Aspect != RequestedAspect)
		{
			DeferredFailure = TEXT("server phase changed at screenshot processing; refusing a mislabeled PNG"); return;
		}
		UE_LOG(LogTrafficSignalVisualQa, Display, TEXT("Capture-time validation %s: [%s] [%s] [%s]"),
			TrafficQaAspectName(RequestedAspect), *Heads[0]->GetStatusText(), *Heads[1]->GetStatusText(), *Heads[2]->GetStatusText());
	}

	bool Tick(float DeltaSeconds)
	{
		if (!DeferredFailure.IsEmpty()) { return Finish(false, DeferredFailure); }
		if (FPlatformTime::Seconds() - StartWallTime >= 45.0)
		{
			return Finish(false, TEXT("bounded 45s timeout: ") + WaitingReason);
		}
		UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
		if (!World || !Player || !GEngine->GameViewport->Viewport) { return true; }
		if (World->WorldType != EWorldType::Game)
		{
			return Finish(false, TEXT("probe only supports a normal -game world, not an authoring/PIE/commandlet world"));
		}
		if (ProbeCamera.IsValid() && (World != GameWorld.Get() || Player != Controller.Get()
			|| Player->GetViewTarget() != ProbeCamera.Get()))
		{
			return Finish(false, TEXT("world/controller/view target changed during QA"));
		}
		TArray<ASimCoreTrafficSignalActor*> Heads;
		if (!GetThreeHeads(World, Heads, WaitingReason))
		{
			if (Heads.Num() > 3 || ProbeCamera.IsValid()) { return Finish(false, WaitingReason); }
			return true;
		}
		if (!ProbeCamera.IsValid())
		{
			if (!PlaceCamera(World, Player, Heads[0])) { return Finish(false, TEXT("transient camera creation failed")); }
		}
		++NormalTickCount;
		NormalTickTime += FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, 0.25);
		const bool bShaderBusy = GShaderCompilingManager
			&& (GShaderCompilingManager->IsCompiling() || GShaderCompilingManager->GetNumRemainingJobs() != 0);
		QuietShaderTicks = bShaderBusy ? 0 : QuietShaderTicks + 1;
		if (bPendingShot)
		{
			if (bShotProcessed && IFileManager::Get().FileSize(*PendingShot) > 0)
			{
				CapturedMask |= 1u << (static_cast<uint32>(RequestedAspect) - 1u);
				UE_LOG(LogTrafficSignalVisualQa, Display, TEXT("Saved verified %s after %d normal ticks (%.2fs): %s"),
					TrafficQaAspectName(RequestedAspect), NormalTickCount, NormalTickTime, *PendingShot);
				bPendingShot = false;
				if (CapturedMask == 7) { return Finish(true, TEXT("observed and captured all three actual signal-1 phases")); }
			}
			else if (FPlatformTime::Seconds() - ShotWallTime > 5.0)
			{
				return Finish(false, TEXT("normal screenshot did not complete as a PNG: ") + PendingShot);
			}
			return true;
		}
		if (NormalTickTime < 10.0 || NormalTickCount < 3 || QuietShaderTicks < 2)
		{
			WaitingReason = FString::Printf(TEXT("camera warmup normalTime=%.2f shaderIdleTicks=%d"), NormalTickTime, QuietShaderTicks); return true;
		}
		for (const auto* Head : Heads)
		{
			if (!Head->GetDisplayState().bVerified || Head->GetDisplayState().RemainingSeconds < 0.5f)
			{
				StableAspectTicks = 0;
				WaitingReason = TEXT("waiting for three fresh server aspects clear of a phase boundary: ") + Head->GetStatusText(); return true;
			}
		}
		const FTrafficQaAspect Aspect = Heads[0]->GetDisplayState().Aspect;
		if (Aspect != LastAspect) { LastAspect = Aspect; StableAspectTicks = 0; }
		++StableAspectTicks;
		const int32 AspectIndex = static_cast<int32>(Aspect) - 1;
		if (AspectIndex < 0 || AspectIndex > 2) { return Finish(false, TEXT("verified head has an unknown aspect")); }
		WaitingReason = FString::Printf(TEXT("waiting for an uncaptured actual phase; current=%s capturedMask=%u"), TrafficQaAspectName(Aspect), CapturedMask);
		if ((CapturedMask & (1u << AspectIndex)) || StableAspectTicks < 2 || FScreenshotRequest::IsScreenshotRequested()) { return true; }
		FString Error;
		if (!ValidateSnapshot(Heads, Error)) { return Finish(false, Error); }
		PendingShot = Directory / FString::Printf(TEXT("signal-1-%s.png"), TrafficQaAspectName(Aspect));
		if (IFileManager::Get().FileExists(*PendingShot)) { return Finish(false, TEXT("refusing to overwrite an existing QA screenshot")); }
		RequestedAspect = Aspect;
		bPendingShot = true;
		bShotProcessed = false;
		ShotWallTime = FPlatformTime::Seconds();
		// Same ordinary game viewport path as VehicleVisualQa/VirtualCityCapture.
		// No HighResShot, shader-only warmup, forced phases, or renderer overrides.
		FScreenshotRequest::RequestScreenshot(PendingShot, false, false);
		UE_LOG(LogTrafficSignalVisualQa, Display, TEXT("Requesting actual %s: [%s] [%s] [%s]; output=%s"),
			TrafficQaAspectName(Aspect), *Heads[0]->GetStatusText(), *Heads[1]->GetStatusText(), *Heads[2]->GetStatusText(), *PendingShot);
		return true;
	}

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle ScreenshotHandle;
	TWeakObjectPtr<UWorld> GameWorld;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<AActor> OriginalViewTarget;
	TWeakObjectPtr<ACameraActor> ProbeCamera;
	TArray<SimCoreVirtualCity::FTrafficSignal> AuthoredSignals;
	FString Directory, PendingShot, DeferredFailure, WaitingReason;
	double StartWallTime = 0, ShotWallTime = 0, NormalTickTime = 0;
	int32 NormalTickCount = 0, QuietShaderTicks = 0, StableAspectTicks = 0;
	uint32 CapturedMask = 0;
	FTrafficQaAspect LastAspect = FTrafficQaAspect::Unknown;
	FTrafficQaAspect RequestedAspect = FTrafficQaAspect::Unknown;
	bool bPendingShot = false, bShotProcessed = false;
};

FTrafficSignalVisualQa TrafficSignalQaService;
FAutoConsoleCommand TrafficSignalQaCommand(TEXT("TrafficVisual.QAViews"),
	TEXT("Editor-build QA for -game: wait for three server heads, validate parent Color/one-hot/NoCollision/lenses and capture signal 1 RED/YELLOW/GREEN within 45s. Load DriveIntegrationEditor first."),
	FConsoleCommandDelegate::CreateLambda([]() { TrafficSignalQaService.Start(); }));
}
