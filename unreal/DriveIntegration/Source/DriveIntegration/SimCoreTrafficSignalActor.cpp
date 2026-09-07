#include "SimCoreTrafficSignalActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SimCoreCoordinateFrames.h"
#include "UObject/ConstructorHelpers.h"

SimCoreTrafficSignals::FDisplayState SimCoreTrafficSignals::EvaluateDisplay(
	const SimCoreProtocol::FTrafficSignalState& Signal,
	bool bNetworkReady, bool bAcceptedSnapshot, double SnapshotAgeSeconds)
{
	FDisplayState Display;
	if (SimCoreProtocol::IsValidTrafficSignalState(Signal))
	{
		Display.Kind = Signal.Kind;
	}
	if (!bNetworkReady || !bAcceptedSnapshot || !FMath::IsFinite(SnapshotAgeSeconds)
		|| SnapshotAgeSeconds < 0.0 || SnapshotAgeSeconds > SnapshotFreshnessSeconds
		|| !SimCoreProtocol::IsValidTrafficSignalState(Signal)
		|| Signal.Aspect == SimCoreProtocol::ETrafficSignalAspect::Unknown)
	{
		return Display;
	}
	Display.Aspect = Signal.Aspect;
	Display.Kind = Signal.Kind;
	Display.bVerified = true;
	Display.bOutOfService = Signal.bOutOfService;
	Display.RemainingSeconds = Signal.RemainingSeconds;
	Display.bRed = !Display.bOutOfService && Signal.Aspect == SimCoreProtocol::ETrafficSignalAspect::Red;
	Display.bYellow = !Display.bOutOfService && Signal.Aspect == SimCoreProtocol::ETrafficSignalAspect::Yellow;
	Display.bGreen = !Display.bOutOfService && Signal.Aspect == SimCoreProtocol::ETrafficSignalAspect::Green;
	return Display;
}

FTransform SimCoreTrafficSignals::BuildDamagedPoleTransform(
	const SimCoreProtocol::FStructureState& Structure, const FVector& PresentationOffsetCm)
{
	if (!SimCoreProtocol::IsValidStructureState(Structure)
		|| Structure.Kind != SimCoreProtocol::EStructureKind::SignalPole || PresentationOffsetCm.ContainsNaN())
	{
		return FTransform::Identity;
	}
	const double Angle = FMath::Clamp(double(Structure.FallAngleRadians), 0.0, UE_DOUBLE_PI / 2.0);
	FVector Direction(SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(Structure.FallDirectionEnu));
	Direction.Z = 0.0;
	Direction = Direction.GetSafeNormal();
	const FVector Up = FVector::UpVector * FMath::Cos(Angle) + Direction * FMath::Sin(Angle);
	const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Up.GetSafeNormal());
	const FQuat Heading(FRotator(0.0, FMath::RadiansToDegrees(Structure.HeadingRadians), 0.0));
	// The server uses this same half-width lift to keep the fallen collision hull above ground.
	const FVector Base = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(
		Structure.BasePositionEnu, PresentationOffsetCm) + FVector(0, 0, 25.0 * FMath::Sin(Angle));
	return FTransform(Tilt * Heading, Base);
}

FTransform SimCoreTrafficSignals::BuildPoleTransform(
	const SimCoreProtocol::FTrafficSignalState& Signal, const FVector& PresentationOffsetCm)
{
	if (!SimCoreProtocol::IsValidTrafficSignalState(Signal) || PresentationOffsetCm.ContainsNaN())
	{
		return FTransform::Identity;
	}
	return FTransform(FRotator(0.0, Signal.HeadingDegrees, 0.0),
		SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(
			Signal.PositionEnu, PresentationOffsetCm));
}

ASimCoreTrafficSignalActor::ASimCoreTrafficSignalActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	bReplicates = false;
	Tags.Add(TEXT("SimCoreRuntimeTrafficSignal"));
	SignalRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SignalRoot"));
	SetRootComponent(SignalRoot);
	SignalRoot->SetMobility(EComponentMobility::Movable);
	Pole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pole"));
	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Lamps.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RedLamp")));
	Lamps.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YellowLamp")));
	Lamps.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GreenLamp")));
	StatusLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusLabel"));
	StatusLabel->SetupAttachment(SignalRoot);
	StatusLabel->SetRelativeLocation(FVector(-27.0, 0.0, 398.0));
	StatusLabel->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));
	StatusLabel->SetHorizontalAlignment(EHTA_Center);
	StatusLabel->SetWorldSize(12.0f);
	StatusLabel->SetText(FText::FromString(TEXT("unverified/stale")));
	StatusLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StatusLabel->SetGenerateOverlapEvents(false);
	StatusLabel->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ColorMaterial = Material.Object;
	for (UStaticMeshComponent* Mesh : {Pole.Get(), Head.Get(), Lamps[0].Get(), Lamps[1].Get(), Lamps[2].Get()})
	{
		Mesh->SetupAttachment(SignalRoot);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->SetStaticMesh(Mesh == Pole || Mesh == Head ? Cube.Object : Sphere.Object);
		Mesh->SetMaterial(0, ColorMaterial);
	}
	Pole->SetRelativeLocation(FVector(0.0, 0.0, 145.0));
	Pole->SetRelativeScale3D(FVector(0.12, 0.12, 2.9));
	Head->SetRelativeLocation(FVector(0.0, 0.0, 330.0));
	Head->SetRelativeScale3D(FVector(0.36, 0.48, 1.1));
	for (int32 Index = 0; Index < Lamps.Num(); ++Index)
	{
		// The -X face looks back towards drivers approaching along the +X heading.
		Lamps[Index]->SetRelativeLocation(FVector(-21.0, 0.0, 363.0 - Index * 33.0));
		Lamps[Index]->SetRelativeScale3D(FVector(0.12, 0.25, 0.25));
	}
}

void ASimCoreTrafficSignalActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	InitializeMaterials();
	SetFailSafe();
	// No authoring/preview world may display this runtime-only adapter.
	SetActorHiddenInGame(!GetWorld() || !GetWorld()->IsGameWorld());
#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(!GetWorld() || !GetWorld()->IsGameWorld());
#endif
}

void ASimCoreTrafficSignalActor::InitializeMaterials()
{
	if (!ColorMaterial || !LampMaterials.IsEmpty()) return;
	HousingMaterial = UMaterialInstanceDynamic::Create(ColorMaterial, this);
	if (HousingMaterial)
	{
		HousingMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.018f, 0.018f, 0.018f));
		Pole->SetMaterial(0, HousingMaterial);
		Head->SetMaterial(0, HousingMaterial);
	}
	for (UStaticMeshComponent* Lamp : Lamps)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ColorMaterial, this);
		LampMaterials.Add(Instance);
		Lamp->SetMaterial(0, Instance);
	}
}

void ASimCoreTrafficSignalActor::ApplyAuthoritativeSignal(
	const SimCoreProtocol::FTrafficSignalState& Signal,
	bool bNetworkReady, bool bAcceptedSnapshot, double SnapshotAgeSeconds,
	const FVector& PresentationOffsetCm)
{
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	const bool bValidGeometry = SimCoreProtocol::IsValidTrafficSignalState(Signal)
		&& !PresentationOffsetCm.ContainsNaN();
	SetActorHiddenInGame(!bGameWorld || !bValidGeometry);
	if (bGameWorld && bValidGeometry)
	{
		SignalId = Signal.SignalId;
		GroupId = Signal.GroupId;
		// A stale lamp update must never stand an already fallen pole back up.
		if (!bHasStructureDamage)
		{
			SetActorTransform(SimCoreTrafficSignals::BuildPoleTransform(Signal, PresentationOffsetCm));
		}
	}
	Display = SimCoreTrafficSignals::EvaluateDisplay(
		Signal, bNetworkReady && bGameWorld && bValidGeometry, bAcceptedSnapshot, SnapshotAgeSeconds);
	if (Display.bVerified && Display.bOutOfService) bBroken = true;
	ApplyDisplay();
}

void ASimCoreTrafficSignalActor::ApplyStructureDamage(
	const SimCoreProtocol::FStructureState& Structure, const FVector& PresentationOffsetCm)
{
	if (!GetWorld() || !GetWorld()->IsGameWorld() || !SimCoreProtocol::IsValidStructureState(Structure)
		|| Structure.Kind != SimCoreProtocol::EStructureKind::SignalPole
		|| Structure.SignalId != SignalId || PresentationOffsetCm.ContainsNaN()) return;
	bHasStructureDamage = true;
	bBroken = bBroken || Structure.bDisabled;
	SetActorTransform(SimCoreTrafficSignals::BuildDamagedPoleTransform(Structure, PresentationOffsetCm));
	ApplyDisplay();
}

void ASimCoreTrafficSignalActor::ClearStructureDamage()
{
	bHasStructureDamage = false;
	bBroken = false;
}

void ASimCoreTrafficSignalActor::SetFailSafe()
{
	Display = {};
	ApplyDisplay();
}

UStaticMeshComponent* ASimCoreTrafficSignalActor::GetLamp(int32 Index) const
{
	return Lamps.IsValidIndex(Index) ? Lamps[Index].Get() : nullptr;
}

FString ASimCoreTrafficSignalActor::GetStatusText() const
{
	if (Display.bOutOfService) return FString::Printf(TEXT("S%u/G%u BROKEN"), SignalId, GroupId);
	if (!Display.bVerified)
	{
		return Display.IsPedestrian()
			? FString::Printf(TEXT("P%u/G%u DON'T WALK (stale)"), SignalId, GroupId)
			: FString::Printf(TEXT("S%u/G%u unverified/stale"), SignalId, GroupId);
	}
	if (Display.IsPedestrian())
	{
		return FString::Printf(TEXT("P%u/G%u %s %.1fs"), SignalId, GroupId,
			Display.bGreen ? TEXT("WALK") : TEXT("DON'T WALK"), Display.RemainingSeconds);
	}
	const TCHAR* Aspect = Display.bGreen ? TEXT("GREEN") : Display.bYellow ? TEXT("YELLOW") : TEXT("RED");
	return FString::Printf(TEXT("S%u/G%u %s %.1fs\nL / S / R"), SignalId, GroupId, Aspect, Display.RemainingSeconds);
}

void ASimCoreTrafficSignalActor::ApplyDisplay()
{
	if (bBroken)
	{
		Display.bOutOfService = true;
		Display.bRed = Display.bYellow = Display.bGreen = false;
	}
	InitializeMaterials();
	const FLinearColor Colors[] = {FLinearColor(1.0f, 0.01f, 0.005f),
		FLinearColor(1.0f, 0.64f, 0.005f), FLinearColor(0.005f, 1.0f, 0.025f)};
	const bool Active[] = {Display.bRed, Display.bYellow, Display.bGreen};
	for (int32 Index = 0; Index < LampMaterials.Num(); ++Index)
	{
		if (Lamps.IsValidIndex(Index))
		{
			Lamps[Index]->SetVisibility(!Display.IsPedestrian() || Index != 1, true);
		}
		if (LampMaterials[Index])
		{
			LampMaterials[Index]->SetVectorParameterValue(TEXT("Color"),
				Active[Index] ? Colors[Index] : FLinearColor(0.012f, 0.012f, 0.012f));
		}
	}
	StatusLabel->SetText(FText::FromString(GetStatusText()));
	StatusLabel->SetTextRenderColor(Display.bVerified ? FColor::White : FColor(255, 150, 80));
}
